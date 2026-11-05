#!/usr/bin/env python3
"""Train a bootstrap MLP; export matching feature vocabulary and C weights.

Requires NumPy for training only. No dependency is needed by smartfilter.
Validation is split by template BEFORE substituting target names.
The printed score describes this small curated corpus, not production accuracy.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re

os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
import numpy as np

CLASSES = ['progress', 'error', 'warning', 'info', 'result', 'stats', 'version', 'unknown']
ANSI = re.compile(r'\x1b\[[0-9;:]*m')
STOP_WORDS = set('b a an and as at be by for from has have in is it of on or the this to was were with will while using used use following here that are into after'.split())


def words(text):
    text = ANSI.sub('', text)
    chunks = [x for x in text.split() if '/' not in x and '$' not in x]
    plain = re.sub(r'[A-Za-z_][A-Za-z0-9_]*_[A-Za-z0-9_]*|_[A-Za-z0-9_]+', ' ', ' '.join(chunks))
    return re.findall(r'[a-z]+', plain.lower())


def features(text):
    tokens = words(text)
    result = set(tokens + [a + ' ' + b for a, b in zip(tokens, tokens[1:])])
    for chunk in ANSI.sub('', text).split():
        if '/' not in chunk and '$' not in chunk and re.search(
                r'(?<![A-Za-z0-9_.-])[A-Za-z_][A-Za-z0-9_.-]*@[<>=~^]*[0-9]',
                chunk.replace('{target}', 'sample')):
            result.add('@version')
    plain = ANSI.sub('', text)
    number = r'(?<![A-Za-z0-9_./$-])[0-9]+(?:[.,][0-9]+)?[ \t]*'
    if re.search(number + r'(?:[kmgt]i?b|bytes?|b)(?![A-Za-z0-9_])', plain, re.I):
        result.add('@size')
    if re.search(number + r'%', plain):
        result.add('@percentage')
    return result


def encode(text, vocabulary):
    present = features(text)
    row = np.array([float(x in present) for x in vocabulary], dtype=np.float64)
    norm = np.sqrt(row.sum())
    return row / norm if norm else row


def softmax(logits):
    values = np.exp(logits - logits.max(axis=1, keepdims=True))
    return values / values.sum(axis=1, keepdims=True)


def train(x, y, seed):
    rng = np.random.default_rng(seed)
    hidden = 24
    params = [rng.normal(0, np.sqrt(2 / x.shape[1]), (x.shape[1], hidden)),
              np.zeros(hidden), rng.normal(0, np.sqrt(2 / hidden), (hidden, len(CLASSES))),
              np.zeros(len(CLASSES))]
    moments = [np.zeros_like(p) for p in params]
    velocities = [np.zeros_like(p) for p in params]
    target = np.eye(len(CLASSES))[y]
    for step in range(1, 701):
        w1, b1, w2, b2 = params
        z = x @ w1 + b1
        h = np.maximum(z, 0)
        probabilities = softmax(h @ w2 + b2)
        delta = (probabilities - target) / len(x)
        back = (delta @ w2.T) * (z > 0)
        gradients = [x.T @ back + 0.001 * w1, back.sum(axis=0),
                     h.T @ delta + 0.001 * w2, delta.sum(axis=0)]
        for i, gradient in enumerate(gradients):
            moments[i] = 0.9 * moments[i] + 0.1 * gradient
            velocities[i] = 0.999 * velocities[i] + 0.001 * gradient * gradient
            params[i] -= 0.015 * (moments[i] / (1 - 0.9 ** step)) / (
                np.sqrt(velocities[i] / (1 - 0.999 ** step)) + 1e-8)
    return params


def predict(x, params):
    w1, b1, w2, b2 = params
    return softmax(np.maximum(x @ w1 + b1, 0) @ w2 + b2)


def emit_array(name, values):
    flat = values.reshape(-1)
    rows = [', '.join(f'{float(v):.9e}f' for v in flat[i:i+6]) for i in range(0, len(flat), 6)]
    return f'static const float {name}[{len(flat)}] = {{\n    ' + ',\n    '.join(rows) + '\n};\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, default=Path(__file__).with_name('log_categories.tsv'))
    parser.add_argument('--output', type=Path, default=Path(__file__).parents[1] / 'level3/model')
    args = parser.parse_args()
    templates = []
    for number, line in enumerate(args.data.read_text().splitlines(), 1):
        if not line or line.startswith('#'):
            continue
        category, text = line.split('\t', 1)
        if category not in CLASSES:
            raise ValueError(f'{args.data}:{number}: invalid category {category}')
        templates.append((CLASSES.index(category), text))
    rng = np.random.default_rng(173)
    train_ids, valid_ids = [], []
    for category in range(len(CLASSES)):
        ids = [i for i, (label, _) in enumerate(templates) if label == category]
        rng.shuffle(ids)
        split = max(1, len(ids) // 5)
        valid_ids.extend(ids[:split])
        train_ids.extend(ids[split:])
    counts = Counter(f for i in train_ids for f in features(templates[i][1]))
    vocabulary = sorted(f for f, count in counts.items() if count >= 2 and
                        any(word not in STOP_WORDS for word in f.split()))

    def dataset(ids, targets=('alpha', 'dmcrypto', 'firmware')):
        rows, labels = [], []
        for i in ids:
            category, template = templates[i]
            for target in targets:
                text = template.replace('{target}', target)
                rows.append(encode(text, vocabulary))
                labels.append(category)
        return np.stack(rows), np.array(labels)

    x, y = dataset(train_ids)
    valid_x, valid_y = dataset(valid_ids, ('sample',))
    params = train(x, y, 741)
    scores = predict(valid_x, params)
    predicted = scores.argmax(axis=1)
    evidence_columns = [i for i, name in enumerate(vocabulary) if ' ' not in name and
                        name not in {'info', 'verbose', 'debug', 'trace'}]
    evidence = (valid_x[:, evidence_columns] > 0).sum(axis=1)
    confident = (evidence >= 2) & (predicted != CLASSES.index('unknown')) & (scores.max(axis=1) >= 0.90) & (
        np.sort(scores, axis=1)[:, -1] - np.sort(scores, axis=1)[:, -2] >= 0.30)
    report = {
        'corpus': 'manually curated bootstrap templates; not production validation',
        'dataset_sha256': hashlib.sha256(args.data.read_bytes()).hexdigest(),
        'training_templates': len(train_ids), 'validation_templates': len(valid_ids),
        'features': len(vocabulary), 'hidden_units': 24,
        'validation_accuracy_before_abstention': float((predicted == valid_y).mean()),
        'validation_confident_coverage': float(confident.mean()),
        'validation_confident_accuracy': float((predicted[confident] == valid_y[confident]).mean())
            if confident.any() else None,
        'classes': CLASSES, 'confidence_threshold': 0.90, 'margin_threshold': 0.30,
        'minimum_known_content_words': 2,
        'confusion_matrix_before_abstention': [
            [int(((valid_y == a) & (predicted == b)).sum()) for b in range(len(CLASSES))]
            for a in range(len(CLASSES))],
    }
    args.output.mkdir(parents=True, exist_ok=True)
    header = ('/* Generated by training/train_level3.py. Do not edit weights by hand. */\n'
              '#ifndef SMARTFILTER_LEVEL3_MODEL_H\n#define SMARTFILTER_LEVEL3_MODEL_H\n'
              f'#define L3_FEATURES {len(vocabulary)}\n#define L3_HIDDEN 24\n#define L3_CLASSES {len(CLASSES)}\n'
              '#define L3_CONFIDENCE 0.90f\n#define L3_MARGIN 0.30f\n'
              'static const char *const l3_vocabulary[L3_FEATURES] = {\n    ' +
              ',\n    '.join(json.dumps(x) for x in vocabulary) + '\n};\n')
    for name, values in zip(('l3_w1', 'l3_b1', 'l3_w2', 'l3_b2'), params):
        header += emit_array(name, values)
    (args.output / 'weights.h').write_text(header + '#endif\n')
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    # Golden reference cases for checking Python/C feature and inference parity.
    reference = []
    for i in valid_ids:
        category, template = templates[i]
        text = template.replace('{target}', 'sample')
        probabilities = predict(encode(text, vocabulary)[None, :], params)[0]
        reference.append({'text': text, 'label': CLASSES[category],
                          'scores': [float(p) for p in probabilities]})
    (args.output / 'reference.json').write_text(json.dumps(reference, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Explicit one-time download of pinned local E5 assets; no logs are uploaded."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import urllib.request

REPOSITORY = 'intfloat/multilingual-e5-small'
REVISION = '614241f622f53c4eeff9890bdc4f31cfecc418b3'
FILES = ['onnx/model.onnx', 'tokenizer.json', 'config.json']


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def valid_local_model(directory, revision):
    try:
        manifest = json.loads((directory / 'manifest.json').read_text())
        if manifest.get('repository') != REPOSITORY or manifest.get('revision') != revision:
            return False
        return all((directory / name).is_file() and
                   manifest.get('files', {}).get(name) == digest(directory / name) for name in FILES)
    except (OSError, ValueError, TypeError, AttributeError):
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--revision', default=REVISION)
    args = parser.parse_args()
    if valid_local_model(args.directory, args.revision):
        print(f'Existing model verified offline: {args.directory}', flush=True)
        return
    with urllib.request.urlopen(f'https://huggingface.co/api/models/{REPOSITORY}/revision/{args.revision}', timeout=60) as stream:
        revision = json.load(stream)['sha']
    args.directory.mkdir(parents=True, exist_ok=True)
    manifest_path = args.directory / 'manifest.json'
    previous = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    hashes = {}
    for name in FILES:
        target = args.directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if (previous.get('revision') == revision and target.is_file() and
                previous.get('files', {}).get(name) == digest(target)):
            print(f'Verified {name}', flush=True)
        else:
            print(f'Downloading {name} at {revision}', flush=True)
            temporary = target.with_name(target.name + '.part')
            with urllib.request.urlopen(f'https://huggingface.co/{REPOSITORY}/resolve/{revision}/{name}', timeout=120) as source:
                with temporary.open('wb') as destination:
                    while chunk := source.read(1024 * 1024):
                        destination.write(chunk)
            os.replace(temporary, target)
        hashes[name] = digest(target)
    manifest = {'repository': REPOSITORY, 'revision': revision, 'files': hashes,
                'embedding': 'mean-pool-attention-mask,l2,query/passage,chunk-overlap64-v1'}
    temporary = manifest_path.with_suffix('.part')
    temporary.write_text(json.dumps(manifest, indent=2) + '\n')
    os.replace(temporary, manifest_path)
    print(f'Ready: {manifest_path}', flush=True)


if __name__ == '__main__':
    main()

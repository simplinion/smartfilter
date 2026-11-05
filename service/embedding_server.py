#!/usr/bin/env python3
"""Resident local embedding service over a versioned Unix-socket protocol.

Only --model-dir assets are loaded. Serving never downloads anything.
"""
import argparse
from collections import OrderedDict
import hashlib
import json
import logging
import os
import re
from pathlib import Path
import signal
import socket
import socketserver
import stat
import struct
import threading

SGR = re.compile(r'\x1b\[[0-9;:]*m')
MAGIC = b'SFE1'
MAX_BATCH = 32
MAX_TEXT = 65536
MAX_FRAME = 4 * 1024 * 1024
MAX_CLIENTS = 16


def default_socket():
    cache = Path(os.environ.get('XDG_CACHE_HOME', str(Path.home() / '.cache')))
    if not cache.is_absolute():
        cache = Path.home() / '.cache'
    return cache / 'smartfilter/embeddings.sock'


def receive(stream, size):
    parts = bytearray()
    while len(parts) < size:
        part = stream.recv(size - len(parts))
        if not part:
            raise EOFError
        parts.extend(part)
    return bytes(parts)


def read_frame(stream):
    magic, size = struct.unpack('!4sI', receive(stream, 8))
    if magic != MAGIC or size > MAX_FRAME:
        raise ValueError('invalid protocol frame')
    return receive(stream, size)


def frame(payload):
    return struct.pack('!4sI', MAGIC, len(payload)) + payload


class E5:
    def __init__(self, directory, threads):
        import numpy as np
        import onnxruntime as ort
        from tokenizers import Tokenizer
        ort.disable_telemetry_events()
        self.np = np
        manifest_bytes = (directory / 'manifest.json').read_bytes()
        manifest = json.loads(manifest_bytes)
        for name, expected in manifest['files'].items():
            path = (directory / name).resolve()
            if not path.is_relative_to(directory.resolve()):
                raise ValueError('invalid manifest path')
            with path.open('rb') as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
            if actual != expected:
                raise ValueError(f'model checksum mismatch: {name}')
        options = ort.SessionOptions()
        options.intra_op_num_threads = threads
        options.inter_op_num_threads = 1
        self.session = ort.InferenceSession(str(directory / 'onnx/model.onnx'), options,
                                           providers=['CPUExecutionProvider'])
        self.inputs = {item.name for item in self.session.get_inputs()}
        if not {'input_ids', 'attention_mask'} <= self.inputs:
            raise ValueError('unsupported ONNX inputs')
        self.tokenizer = Tokenizer.from_file(str(directory / 'tokenizer.json'))
        self.tokenizer.no_truncation()
        self.tokenizer.no_padding()
        self.identity = ('e5:' + manifest['revision'] + ':' +
                         hashlib.sha256(manifest_bytes).hexdigest() + ':pool-v1')
        self.dimension = 384
        self.encode([(0, 'warmup')])

    def encode(self, items):
        np = self.np
        chunks, owners = [], []
        for owner, (role, text) in enumerate(items):
            prefix = 'query: ' if role == 0 else 'passage: '
            prefix_ids = self.tokenizer.encode(prefix.rstrip()).ids[1:-1]
            encoded = self.tokenizer.encode(prefix + text).ids
            if encoded[1:1 + len(prefix_ids)] != prefix_ids:
                raise ValueError('unexpected tokenizer prefix layout')
            body = encoded[1 + len(prefix_ids):-1]
            capacity = 512 - len(prefix_ids) - 2
            offsets = list(range(0, max(1, len(body)), capacity - 64))
            if len(offsets) > 64:
                raise ValueError('text needs too many token chunks')
            for offset in offsets:
                chunks.append([encoded[0]] + prefix_ids + body[offset:offset + capacity] + [encoded[-1]])
                owners.append(owner)
                if offset + capacity >= len(body):
                    break
        collected = [[] for _ in items]
        for start in range(0, len(chunks), 8):
            batch = chunks[start:start + 8]
            width = max(map(len, batch))
            ids = np.full((len(batch), width), self.tokenizer.token_to_id('<pad>'), dtype=np.int64)
            mask = np.zeros_like(ids)
            for i, values in enumerate(batch):
                ids[i, :len(values)] = values
                mask[i, :len(values)] = 1
            feed = {'input_ids': ids, 'attention_mask': mask}
            if 'token_type_ids' in self.inputs:
                feed['token_type_ids'] = np.zeros_like(ids)
            output = self.session.run(None, feed)[0]
            if output.ndim != 3 or output.shape[2] != self.dimension:
                raise ValueError('unsupported ONNX output')
            pooled = (output * mask[:, :, None]).sum(axis=1) / mask.sum(axis=1)[:, None]
            pooled /= np.maximum(np.linalg.norm(pooled, axis=1, keepdims=True), 1e-12)
            for i, value in enumerate(pooled):
                collected[owners[start + i]].append(value)
        result = []
        for values in collected:
            value = np.mean(values, axis=0)
            norm = np.linalg.norm(value)
            if not np.isfinite(norm) or norm <= 0:
                raise ValueError('invalid embedding')
            result.append(np.asarray(value / norm, dtype=np.float32))
        return result


class Embeddings:
    def __init__(self, backend, cache_bytes):
        self.backend = backend
        self.cache = OrderedDict()
        self.maximum = cache_bytes
        self.used = 0
        self.lock = threading.Lock()

    def encode(self, items):
        items = [(role, SGR.sub('', text)) for role, text in items]
        # One bounded inference batch at a time; all clients share this model
        # and the same role/text LRU. No per-command model initialization.
        with self.lock:
            present = {item: self.cache[item][0] for item in items if item in self.cache}
            missing = list(dict.fromkeys(item for item in items if item not in self.cache))
            computed = dict(zip(missing, self.backend.encode(missing))) if missing else {}
            values = {**present, **computed}
            result = []
            for item in items:
                if item in self.cache:
                    value, size = self.cache.pop(item)
                    self.cache[item] = (value, size)
                else:
                    value = values[item]
                    size = len(item[1].encode()) + value.nbytes + 128
                    if size <= self.maximum:
                        while self.used + size > self.maximum and self.cache:
                            _, (_, removed) = self.cache.popitem(last=False)
                            self.used -= removed
                        self.cache[item] = (value, size)
                        self.used += size
                result.append(value)
            return result


def decode_request(data):
    if len(data) < 4:
        raise ValueError('missing batch size')
    count, = struct.unpack_from('!I', data)
    if count > MAX_BATCH:
        raise ValueError('batch too large')
    offset, items = 4, []
    for _ in range(count):
        if offset + 5 > len(data):
            raise ValueError('incomplete item')
        role, size = struct.unpack_from('!BI', data, offset)
        offset += 5
        if role > 1 or size > MAX_TEXT or offset + size > len(data):
            raise ValueError('invalid role or text size')
        text = data[offset:offset + size].decode('utf-8')
        if not text.strip() or '\x00' in text:
            raise ValueError('empty or binary text')
        offset += size
        items.append((role, text))
    if offset != len(data):
        raise ValueError('trailing bytes')
    return items


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        # Compiler output can pause for minutes. The C client owns each RPC
        # deadline; keep its resident-model connection through idle periods.
        self.request.settimeout(None)
        try:
            while True:
                data = read_frame(self.request)
                try:
                    items = decode_request(data)
                    service = self.server.embeddings
                    vectors = service.encode(items) if items else []
                    identity = service.backend.identity.encode()
                    payload = struct.pack('!IIII', 0, service.backend.dimension, len(items), len(identity)) + identity
                    payload += b''.join(value.astype('>f4').tobytes() for value in vectors)
                except Exception as error:
                    logging.error('embedding request failed: %s', type(error).__name__)
                    payload = struct.pack('!I', 1) + b'embedding request failed'
                self.request.sendall(frame(payload))
        except (EOFError, OSError, ValueError):
            return


class Server(socketserver.ThreadingUnixStreamServer):
    daemon_threads = True
    request_queue_size = MAX_CLIENTS

    def __init__(self, path, embeddings):
        self.embeddings = embeddings
        self.slots = threading.BoundedSemaphore(MAX_CLIENTS)
        super().__init__(str(path), Handler)

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            request.close()
            return
        try:
            super().process_request(request, address)
        except Exception:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.slots.release()


def serve(path, embeddings):
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    # Refuse to remove anything that is not our stale socket. A probe is only
    # advisory; a separate lifetime lock prevents competing startup/removal.
    import fcntl
    lock_path = path.parent / (path.name + '.lock')
    descriptor = os.open(lock_path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
    with os.fdopen(descriptor, 'r+') as lock:
        if os.fstat(lock.fileno()).st_uid != os.getuid():
            raise ValueError('lock belongs to another user')
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if path.exists() or path.is_symlink():
            info = path.lstat()
            if not stat.S_ISSOCK(info.st_mode) or info.st_uid != os.getuid():
                raise ValueError('socket path is not an owned socket')
            with socket.socket(socket.AF_UNIX) as probe:
                if probe.connect_ex(str(path)) == 0:
                    raise ValueError('socket is already serving')
            path.unlink()
        previous = os.umask(0o077)
        try:
            server = Server(path, embeddings)
        finally:
            os.umask(previous)
        inode = path.stat().st_ino
        stopping = threading.Event()

        def stop(*_):
            if not stopping.is_set():
                stopping.set()
                threading.Thread(target=server.shutdown, daemon=True).start()

        for sig in (signal.SIGTERM, signal.SIGINT):
            signal.signal(sig, stop)
        logging.info('ready: socket=%s model=%s', path, embeddings.backend.identity)
        try:
            server.serve_forever(poll_interval=0.1)
        finally:
            server.server_close()
            if path.exists() and path.stat().st_ino == inode:
                path.unlink()


def check(path):
    with socket.socket(socket.AF_UNIX) as stream:
        stream.settimeout(5)
        stream.connect(str(path))
        stream.sendall(frame(struct.pack('!I', 0)))
        payload = read_frame(stream)
        status, dimension, count, length = struct.unpack_from('!IIII', payload)
        if status or count or len(payload) != 16 + length:
            raise ValueError('invalid health response')
        print(json.dumps({'ready': True, 'dimension': dimension, 'model': payload[16:].decode()}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--socket', type=Path, default=default_socket())
    parser.add_argument('--model-dir', type=Path)
    parser.add_argument('--threads', type=int, default=2)
    parser.add_argument('--cache-mib', type=int, default=64)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format='smartfilter-embeddings: %(message)s')
    if args.check:
        check(args.socket)
        return
    if not args.model_dir:
        parser.error('--model-dir is required')
    if not 1 <= args.threads <= 64 or not 0 <= args.cache_mib <= 4096:
        parser.error('invalid thread/cache limit')
    backend = E5(args.model_dir, args.threads)
    serve(args.socket, Embeddings(backend, args.cache_mib * 1024 * 1024))


if __name__ == '__main__':
    main()

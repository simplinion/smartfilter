"""Wire protocol, event ownership, batching and fail-open; no model dependencies.

The deterministic backend is a test double, never a production fallback.
"""
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import math
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('embedding_server', ROOT / 'service/embedding_server.py')
service = importlib.util.module_from_spec(spec)
spec.loader.exec_module(service)
exe = str(Path(sys.argv[1]).resolve())


class Vector:
    def __init__(self, values):
        self.values = values
        self.nbytes = len(values) * 4
    def astype(self, _):
        return self
    def tobytes(self):
        return struct.pack('!%sf' % len(self.values), *self.values)


class Backend:
    dimension = 3
    identity = 'test-only-backend-v1'
    def __init__(self):
        self.calls = []
    def encode(self, items):
        self.calls.append(items)
        result = []
        for role, text in items:
            if 'NAN-VECTOR' in text:
                result.append(Vector([float('nan'), 0, 0]))
            else:
                result.append(Vector([1, 0, 0] if role == 0 or 'MEMORY-TOPIC' in text else [0, 1, 0]))
        return result


with tempfile.TemporaryDirectory(prefix='sf-semantic-') as directory:
    path = str(Path(directory) / 'e.sock')
    backend = Backend()
    embeddings = service.Embeddings(backend, 1024 * 1024)
    server = service.Server(path, embeddings)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    def run(text, *flags, query='memory', selected_path=path):
        p = subprocess.run([exe, '--query', query, '--embedding-socket', selected_path, *flags],
                           input=text, capture_output=True, timeout=15)
        assert p.returncode == 0, p.stderr
        return p

    text = b'MEMORY-TOPIC usage\nunrelated text\n'
    assert run(text).stdout == b'[sim +1.000] MEMORY-TOPIC usage\n[sim +0.000] unrelated text\n'
    assert run(text, '--min-score=0.5').stdout == b'[sim +1.000] MEMORY-TOPIC usage\n'
    before = len(backend.calls)
    assert run(text).stdout.startswith(b'[sim +1.000]')
    assert len(backend.calls) == before  # Query and passages cached across processes.
    assert run(b'memory\n').stdout == b'[sim +0.000] memory\n'  # Roles have separate keys.
    before_style = len(backend.calls)
    styled = b'\x1b[34mMEMORY-TOPIC styled\x1b[0m\n'
    assert run(styled).stdout == b'[sim +1.000] ' + styled
    assert run(b'MEMORY-TOPIC styled\n').stdout == b'[sim +1.000] MEMORY-TOPIC styled\n'
    assert len(backend.calls) == before_style + 1
    batch = b'MEMORY-TOPIC first\nother second\nMEMORY-TOPIC third\n'
    assert run(batch, '--semantic-batch=3').stdout.count(b'[sim ') == 3
    assert len(backend.calls[-1]) == 3

    event = (b"a.c: In function 'f':\n"
             b'a.c:7:4: warning: MEMORY-TOPIC overflow\n'
             b'    7 | foo();\n'
             b'      |    ^\n')
    out = run(event + b'ordinary\n', '--min-score=0.5').stdout
    assert out == b''.join(b'[sim +1.000] ' + line for line in event.splitlines(keepends=True))
    # One embedding input includes the complete original event, not just the
    # last caret, compacted suffix, category/color prefix, or debug boundary.
    calls = [item for call in backend.calls for item in call]
    assert (1, event.decode()) in calls
    display = run(event, '-c', '--classify', '--color', '--events').stdout
    assert b'[event 1 begin warning' in display and b'[event 1 end]' in display
    assert b'[sim +0.000]' not in display
    assert all(b'[sim +1.000]' in line for line in display.splitlines())
    # Service loss must not remove input, even with an explicit filter threshold.
    missing = run(text, '--min-score=0.99', selected_path=str(Path(directory) / 'missing'))
    assert missing.stdout == text and b'keeping unscored' in missing.stderr
    invalid = run(b'NAN-VECTOR\nMEMORY-TOPIC after\n', '--min-score=0.99')
    assert invalid.stdout == b'NAN-VECTOR\nMEMORY-TOPIC after\n'
    assert invalid.stderr.count(b'level5 disabled') == 1
    binary = b'x\x00y\n\n'
    assert run(binary, '--min-score=1').stdout == binary
    huge = b'x' * 70000 + b'\n'
    assert run(huge, '--min-score=1').stdout == huge
    huge_event = event + (b'      |' + b'^' * 1000 + b'\n') * 40
    assert run(huge_event, '--min-score=1').stdout == huge_event
    assert run(b'MEMORY-TOPIC last', '--min-score=.5').stdout == b'[sim +1.000] MEMORY-TOPIC last'
    assert run(b'').stdout == b''

    def simultaneous(_):
        return run(b'MEMORY-TOPIC concurrent\n').stdout
    with ThreadPoolExecutor(max_workers=4) as pool:
        assert all(x == b'[sim +1.000] MEMORY-TOPIC concurrent\n' for x in pool.map(simultaneous, range(8)))

    # Malformed, truncated and oversized requests do not kill the service.
    for data in [struct.pack('!I', 33), struct.pack('!I', 1) + b'\x05' + struct.pack('!I', 1) + b'x',
                 struct.pack('!I', 1) + b'\x00' + struct.pack('!I', 1) + b'\xff']:
        with socket.socket(socket.AF_UNIX) as stream:
            stream.settimeout(2)
            stream.connect(path)
            stream.sendall(service.frame(data))
            assert struct.unpack_from('!I', service.read_frame(stream))[0] == 1
    assert run(text).stdout.startswith(b'[sim +1.000]')
    # Finite vector validation and cache byte limits are deterministic.
    tiny = service.Embeddings(Backend(), 1)
    tiny.encode([(0, 'test')])
    assert tiny.used == 0 and not tiny.cache
    lru = service.Embeddings(Backend(), 200)
    lru.encode([(1, 'cached')])
    assert len(lru.encode([(1, 'new'), (1, 'cached')])) == 2
    assert lru.used <= 200
    server.shutdown()
    server.server_close()
    thread.join(timeout=2)

    # A restart/model replacement cannot silently mix vector spaces.
    class ChangingBackend(Backend):
        identity = 'before'
        def encode(self, items):
            values = super().encode(items)
            if any(role == 1 for role, _ in items):
                self.identity = 'after'
            return values
    changed_path = str(Path(directory) / 'changed')
    changed_server = service.Server(changed_path, service.Embeddings(ChangingBackend(), 1000))
    changed_thread = threading.Thread(target=changed_server.serve_forever, daemon=True)
    changed_thread.start()
    changed = run(text, '--min-score=.99', selected_path=changed_path)
    assert changed.stdout == text and b'model changed' in changed.stderr
    changed_server.shutdown()
    changed_server.server_close()
    changed_thread.join(timeout=2)

    # A nonresponsive peer is bounded by the single RPC deadline.
    stalled_path = str(Path(directory) / 'stalled')
    stalled = socket.socket(socket.AF_UNIX)
    stalled.bind(stalled_path)
    stalled.listen(1)
    connected = threading.Event()
    stop = threading.Event()
    def stall():
        connection, _ = stalled.accept()
        connected.set()
        stop.wait(2)
        connection.close()
    worker = threading.Thread(target=stall, daemon=True)
    worker.start()
    start = time.monotonic()
    timeout = run(text, '--semantic-timeout=100', '--min-score=.99', selected_path=stalled_path)
    assert timeout.stdout == text and b'timed out' in timeout.stderr
    assert time.monotonic() - start < 2
    stop.set()
    worker.join(timeout=2)
    stalled.close()

for flags in [('--min-score=.5',), ('--query=',), ('--query=   ',),
              ('--query=x', '--min-score=nan'), ('--query=x', '--min-score=2'),
              ('--query=x', '--semantic-batch=33'), ('--query=x', '--semantic-timeout=0')]:
    assert subprocess.run([exe,*flags],input=b'',capture_output=True).returncode != 0
print('Semantic protocol, concurrency and fail-open checks passed')

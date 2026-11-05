"""Resident service readiness, socket ownership, startup races and shutdown."""
import importlib.util
from pathlib import Path
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('embedding_server', ROOT / 'service/embedding_server.py')
service = importlib.util.module_from_spec(spec)
spec.loader.exec_module(service)

if len(sys.argv) > 1 and sys.argv[1] == '--worker':
    class Backend:
        identity = 'test-lifecycle-only'
        dimension = 3
    service.serve(Path(sys.argv[2]), service.Embeddings(Backend(), 1000))
    raise SystemExit


def start(path):
    return subprocess.Popen([sys.executable, __file__, '--worker', str(path)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def health(path):
    with socket.socket(socket.AF_UNIX) as peer:
        peer.settimeout(1)
        peer.connect(str(path))
        peer.sendall(service.frame(struct.pack('!I', 0)))
        data = service.read_frame(peer)
        assert struct.unpack_from('!IIII', data) == (0, 3, 0, len('test-lifecycle-only'))


with tempfile.TemporaryDirectory(prefix='sf-service-') as temporary:
    path = Path(temporary) / 'e.sock'
    # A stale Unix socket is replaced, without touching arbitrary files.
    with socket.socket(socket.AF_UNIX) as stale:
        stale.bind(str(path))
    process = start(path)
    try:
        deadline = time.monotonic() + 5
        while True:
            try:
                health(path)
                break
            except (OSError, EOFError):
                assert process.poll() is None
                assert time.monotonic() < deadline
                time.sleep(.02)
        assert path.stat().st_mode & 0o777 == 0o700  # bind honors the 0077 umask.
        for _ in range(3):
            health(path)
            assert process.poll() is None
        rival = start(path)
        rival.communicate(timeout=3)
        assert rival.returncode != 0
        health(path)  # Losing startup did not unlink the live service socket.
        check = subprocess.run([sys.executable, str(ROOT / 'service/embedding_server.py'),
                                '--check', '--socket', str(path)], capture_output=True)
        assert check.returncode == 0 and b'"ready": true' in check.stdout
    finally:
        process.terminate()
        process.communicate(timeout=3)
    assert not path.exists()
    path.write_text('must not be removed')
    blocked = start(path)
    blocked.communicate(timeout=3)
    assert blocked.returncode != 0 and path.read_text() == 'must not be removed'
    path.unlink()
    victim = Path(temporary) / 'victim'
    victim.write_text('untouched')
    lock = Path(str(path) + '.lock')
    lock.unlink()
    lock.symlink_to(victim)
    blocked = start(path)
    blocked.communicate(timeout=3)
    assert blocked.returncode != 0 and victim.read_text() == 'untouched'
print('Service lifecycle and socket safety checks passed')

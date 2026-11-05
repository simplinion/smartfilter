"""Optional real E5 integration, separate from dependency-free CTest.

python tests/semantic_real_test.py ./build/smartfilter /path/to/socket
python tests/semantic_real_test.py ./build/smartfilter --model-dir build/semantic-model
The latter starts the latest service for the test and stops it afterwards.
Never downloads assets or enables autostart.
"""
import argparse
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time


def exercise(exe, path):
    text = ('a.c:5:3: error: heap allocation failed: out of memory\n'
            '    5 | char *buffer = malloc(4096);\n'
            '      | ^~~~~~~~~~~~~~~~~~~~~~~~~\n'
            'Downloading archive from the package repository\n'
            '[100%] Built target firmware\n')
    args = [exe, '--query', 'problemy z pamięcią i alokacją', '--embedding-socket', str(path)]
    def run(extra=()):
        p = subprocess.run([*args, *extra], input=text.encode(), capture_output=True, timeout=30)
        assert p.returncode == 0 and not p.stderr, p.stderr
        return p.stdout.decode()
    result = run()
    scores = [float(x) for x in re.findall(r'^\[sim ([^\]]+)\]', result, re.M)]
    assert len(scores) == 5, result
    assert scores[0] == scores[1] == scores[2], result
    assert scores[0] > scores[3] and scores[0] > scores[4], result
    threshold = (scores[0] + max(scores[3:])) / 2
    selected = run((f'--min-score={threshold}', '--semantic-batch=3'))
    assert 'malloc' in selected and 'out of memory' in selected
    assert 'Downloading' not in selected and 'Built target' not in selected
    assert run() == result
    print(result)
    print(f'Real E5 Polish/English retrieval passed; demonstration threshold {threshold:.4f}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('socket', nargs='?')
    parser.add_argument('--model-dir', type=Path)
    args = parser.parse_args()
    if bool(args.socket) == bool(args.model_dir):
        parser.error('provide either a socket or --model-dir')
    if args.socket:
        exercise(args.executable, args.socket)
        return
    with tempfile.TemporaryDirectory(prefix='sf-real-') as directory:
        path = Path(directory) / 'service.sock'
        helper = Path(__file__).resolve().parents[1] / 'service/embedding_server.py'
        process = subprocess.Popen([sys.executable, str(helper), '--model-dir', str(args.model_dir),
                                    '--socket', str(path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 60
            while True:
                with socket.socket(socket.AF_UNIX) as peer:
                    if peer.connect_ex(str(path)) == 0:
                        break
                if process.poll() is not None:
                    raise AssertionError(process.communicate()[1].decode())
                if time.monotonic() >= deadline:
                    raise AssertionError('model readiness timeout')
                time.sleep(.05)
            exercise(args.executable, path)
            assert process.poll() is None
        finally:
            if process.poll() is None:
                process.terminate()
            process.communicate(timeout=10)
        assert not path.exists(), 'service did not clean up its socket'


if __name__ == '__main__':
    main()

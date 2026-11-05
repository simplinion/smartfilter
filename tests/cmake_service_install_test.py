"""Exercise the actual install hook with an isolated Python launcher double."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='sf-cmake-install-') as temporary:
    root = Path(temporary)
    launcher = root / 'tools/python3'
    launcher.parent.mkdir()
    log = root / 'called.json'
    launcher.write_text('#!' + sys.executable + '\nimport json,sys\nfrom pathlib import Path\n'
                        'Path(' + repr(str(log)) + ').write_text(json.dumps(sys.argv[1:]))\n')
    launcher.chmod(0o700)
    env = dict(os.environ)
    env.pop('DESTDIR', None)
    env['PATH'] = str(launcher.parent) + os.pathsep + env['PATH']
    prefix = root / 'actual prefix with spaces'
    subprocess.run(['cmake', '--install', str(build), '--prefix', str(prefix)],
                   env=env, check=True, capture_output=True)
    called = json.loads(log.read_text())
    assert called[0] == str(prefix / 'share/smartfilter/install_service.py')
    assert called[called.index('--prefix') + 1] == str(prefix)
    assert (prefix / 'bin/smartfilter').is_file()
    log.unlink()
    stage = root / 'stage'
    staged = subprocess.run(['cmake', '--install', str(build), '--prefix', '/opt/smartfilter'],
                            env={**env, 'DESTDIR': str(stage)}, check=True, capture_output=True)
    assert b'activation skipped' in staged.stdout and not log.exists()
    assert (stage / 'opt/smartfilter/bin/smartfilter').is_file()
    # OFF really bypasses the helper in a normal (non-DESTDIR) installation.
    disabled = root / 'off-build'
    subprocess.run(['cmake', '-S', str(ROOT), '-B', str(disabled),
                    '-DBUILD_TESTING=OFF', '-DSMARTFILTER_INSTALL_SERVICE=OFF'],
                   check=True, capture_output=True)
    shutil.copy2(build / 'smartfilter', disabled / 'smartfilter')
    subprocess.run(['cmake', '--install', str(disabled), '--prefix', str(root / 'off-prefix')],
                   env=env, check=True, capture_output=True)
    assert not log.exists()
print('CMake install activation, runtime prefix, DESTDIR and OFF checks passed')

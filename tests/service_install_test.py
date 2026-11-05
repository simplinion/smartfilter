"""Isolated service-install orchestration; never modifies the real user account."""
from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('installer', ROOT / 'service/install_service.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)
spec = importlib.util.spec_from_file_location('downloader', ROOT / 'service/download_model.py')
downloader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(downloader)


def model(directory):
    manifest = {'repository': downloader.REPOSITORY, 'revision': downloader.REVISION, 'files': {}}
    for name in downloader.FILES:
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(('test data: ' + name).encode())
        manifest['files'][name] = hashlib.sha256(path.read_bytes()).hexdigest()
    (directory / 'manifest.json').write_text(json.dumps(manifest))


with tempfile.TemporaryDirectory(prefix='sf-install-') as temporary:
    base = Path(temporary)
    prefix = base / 'prefix with spaces $literal%value'
    scripts = prefix / 'bin'
    scripts.mkdir(parents=True)
    shutil.copyfile(ROOT / 'service/embedding_server.py', scripts / 'smartfilter-embeddings')
    shutil.copyfile(ROOT / 'service/download_model.py', scripts / 'smartfilter-download-model')
    data = prefix / 'share/smartfilter'
    data.mkdir(parents=True)
    shutil.copyfile(ROOT / 'service/requirements.txt', data / 'requirements.txt')
    source = base / 'build-cache'
    model(source)
    state, config = base / 'state with spaces', base / 'user-config'
    calls = []
    def runner(command, **kwargs):
        calls.append(command)
        if '-m' in command and 'venv' in command:
            python = state / 'venv/bin/python'
            python.parent.mkdir(parents=True)
            python.touch()
        return subprocess.CompletedProcess(command, 0, stdout='{"ready": true}\n', stderr='')
    with patch.object(installer.os, 'geteuid', return_value=1000), redirect_stdout(io.StringIO()):
        installer.install(prefix, Path('bin'), Path('share'), source, runner=runner, state=state, config=config)
        assert downloader.valid_local_model(state / 'models/e5', downloader.REVISION)
        unit = (config / installer.UNIT).read_text()
        assert installer.argument(prefix / 'bin/smartfilter-embeddings') in unit
        assert installer.argument(state / 'venv/bin/python') in unit
        assert ['systemctl', '--user', 'enable', installer.UNIT] in calls
        assert ['systemctl', '--user', 'restart', installer.UNIT] in calls
        assert sum('pip' in c for c in calls) == 1
        calls.clear()
        # Reinstallation verifies assets and imports without pip or downloads.
        installer.install(prefix, Path('bin'), Path('share'), source, runner=runner, state=state, config=config)
        assert not any('pip' in c or 'venv' in c or any('download-model' in x for x in c) for c in calls)
        assert ['systemctl', '--user', 'restart', installer.UNIT] in calls
        assert installer.argument(Path('/tmp/with%percent$literal')) == '"/tmp/with%%percent$$literal"'
        try:
            installer.unit_text('/a\nb', '/server', '/model')
            raise AssertionError('newline accepted')
        except ValueError:
            pass
        # A user-session failure occurs before environment/model setup.
        failed_calls = []
        def unavailable(command, **kwargs):
            failed_calls.append(command)
            return subprocess.CompletedProcess(command, 1, stdout='', stderr='no user bus')
        try:
            installer.install(prefix, Path('bin'), Path('share'), source,
                              runner=unavailable, state=base / 'unused', config=base / 'unused-config')
            raise AssertionError('missing manager accepted')
        except RuntimeError:
            pass
        assert len(failed_calls) == 1 and not (base / 'unused').exists()
        # A corrupted cache is not silently reused.
        (source / 'tokenizer.json').write_text('corrupt')
        assert not downloader.valid_local_model(source, downloader.REVISION)
    with patch.object(installer.os, 'geteuid', return_value=0):
        try:
            installer.install(prefix, Path('bin'), Path('share'), runner=runner)
            raise AssertionError('root user install accepted')
        except RuntimeError:
            pass
    # An already verified model can be checked wholly offline by the downloader.
    with patch.object(downloader.urllib.request, 'urlopen', side_effect=AssertionError('network access')):
        with patch.object(sys, 'argv', ['download', str(state / 'models/e5')]), redirect_stdout(io.StringIO()):
            downloader.main()
print('Service installer idempotency, prefix, cache and failure checks passed')

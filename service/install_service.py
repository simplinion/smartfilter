#!/usr/bin/env python3
"""Prepare and activate smartfilter's current-user embedding service.

Called by the CMake install hook. No sudo or system-wide service is used.
"""
import argparse
import hashlib
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import time

UNIT = 'smartfilter-embeddings.service'


def atomic_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix='.' + path.name, dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w') as stream:
            stream.write(text)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def argument(value):
    text = str(value)
    if any(ch in text for ch in '\n\r\x00'):
        raise ValueError('invalid path in service unit')
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%').replace('$', '$$') + '"'


def unit_text(python, server, model, socket=None):
    return ('# Managed by smartfilter install_service.py\n'
            '[Unit]\nDescription=Smartfilter local embedding service\n\n'
            '[Service]\nType=simple\nExecStart=' + argument(python) + ' ' + argument(server) +
            ' --model-dir ' + argument(model) + (' --socket ' + argument(socket) if socket else '') + '\nRestart=on-failure\nRestartSec=3\n'
            'TimeoutStopSec=10\nUMask=0077\nNoNewPrivileges=true\n\n'
            '[Install]\nWantedBy=default.target\n')


def xdg_directory(name, fallback):
    value = os.environ.get(name)
    return Path(value) if value and Path(value).is_absolute() else fallback


def run_required(runner, command, **kwargs):
    result = runner([str(x) for x in command], **kwargs)
    if result.returncode:
        raise RuntimeError('command failed: ' + ' '.join(str(x) for x in command))
    return result


def prepare_model(destination, source, downloader, runner):
    definitions = runpy.run_path(str(downloader))
    revision = definitions['REVISION']
    verify = definitions['valid_local_model']
    if verify(destination, revision):
        print('smartfilter: existing model verified', flush=True)
        return
    if source and verify(source, revision):
        print(f'smartfilter: reusing model from {source}', flush=True)
        destination.mkdir(parents=True, exist_ok=True)
        for name in definitions['FILES']:
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            descriptor, temporary = tempfile.mkstemp(prefix='.' + target.name, dir=target.parent)
            os.close(descriptor)
            try:
                shutil.copyfile(source / name, temporary)
                os.replace(temporary, target)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
        atomic_text(destination / 'manifest.json', (source / 'manifest.json').read_text())
        if not verify(destination, revision):
            raise RuntimeError('copied model verification failed')
        return
    run_required(runner, [sys.executable, downloader, destination])
    if not verify(destination, revision):
        raise RuntimeError('downloaded model verification failed')


def install(prefix, bindir, datadir, source=None, *, runner=subprocess.run,
            state=None, config=None, readiness_seconds=90):
    # A user service must not accidentally be installed for root by sudo.
    if os.geteuid() == 0:
        raise RuntimeError('run cmake --install as your normal user with a user-writable prefix; '
                           'or configure -DSMARTFILTER_INSTALL_SERVICE=OFF for a system/package install')
    if sys.version_info < (3, 11):
        raise RuntimeError('embedding service setup requires Python 3.11 or later')
    home = Path.home()
    state = state or xdg_directory('XDG_DATA_HOME', home / '.local/share') / 'smartfilter'
    config = config or xdg_directory('XDG_CONFIG_HOME', home / '.config') / 'systemd/user'
    server = prefix / bindir / 'smartfilter-embeddings'
    downloader = prefix / bindir / 'smartfilter-download-model'
    requirements = prefix / datadir / 'smartfilter/requirements.txt'
    for path in (server, downloader, requirements):
        if not path.is_file():
            raise RuntimeError(f'missing installed file: {path}')
    # Check the user session BEFORE downloading hundreds of megabytes.
    result = runner(['systemctl', '--user', 'show-environment'],
                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if result.returncode:
        raise RuntimeError('user systemd manager unavailable; run installation in your user session '
                           'or configure -DSMARTFILTER_INSTALL_SERVICE=OFF')
    venv = state / 'venv'
    python = venv / 'bin/python'
    if not python.is_file():
        run_required(runner, [sys.executable, '-m', 'venv', venv])
    fingerprint = hashlib.sha256(requirements.read_bytes()).hexdigest()
    stamp = venv / '.smartfilter-requirements.sha256'
    imports = [str(python), '-c', 'import numpy, onnxruntime, tokenizers']
    healthy = runner(imports, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE).returncode == 0
    if not healthy or not stamp.is_file() or stamp.read_text().strip() != fingerprint:
        run_required(runner, [python, '-m', 'pip', 'install', '-r', requirements])
        run_required(runner, imports, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        atomic_text(stamp, fingerprint + '\n')
    else:
        print('smartfilter: existing Python environment verified', flush=True)
    socket = xdg_directory('XDG_CACHE_HOME', home / '.cache') / 'smartfilter/embeddings.sock'
    model = state / 'models/e5'
    prepare_model(model, source, downloader, runner)
    # Actual runtime install prefix, including cmake --install --prefix overrides.
    atomic_text(config / UNIT, unit_text(python, server, model, socket))
    run_required(runner, ['systemctl', '--user', 'daemon-reload'])
    run_required(runner, ['systemctl', '--user', 'enable', UNIT])
    # restart starts an inactive unit too, and reloads code on upgrades once.
    run_required(runner, ['systemctl', '--user', 'restart', UNIT])
    deadline = time.monotonic() + readiness_seconds
    while True:
        result = runner([str(python), str(server), '--check', '--socket', str(socket)], capture_output=True, text=True, timeout=10)
        if result.returncode == 0:
            print('smartfilter: embedding service ready\n' + result.stdout.strip(), flush=True)
            return
        if time.monotonic() >= deadline:
            raise RuntimeError('service did not become ready; inspect journalctl --user -u ' + UNIT)
        time.sleep(.5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', required=True, type=Path)
    parser.add_argument('--bindir', default='bin', type=Path)
    parser.add_argument('--datadir', default='share', type=Path)
    parser.add_argument('--model-source', type=Path)
    args = parser.parse_args()
    try:
        install(args.prefix.resolve(), args.bindir, args.datadir, args.model_source)
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f'smartfilter service installation failed: {error}\n')


if __name__ == '__main__':
    main()

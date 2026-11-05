"""Classifier contract and exported Python/C numerical parity, no NumPy needed."""
import json
import hashlib
import re
from pathlib import Path
import subprocess
import sys

exe, probe, reference_path = sys.argv[1:]

def run(text, *flags):
    p = subprocess.run([exe, *flags], input=text.encode(), capture_output=True)
    assert p.returncode == 0, p.stderr
    return p.stdout.decode()


text = ('[INFO] Downloading module: firmware\n'
        'Building C object firmware.o\n'
        'Linking C executable firmware\n'
        '[ERROR] Downloading failed\n'
        'warning: unused variable\n'
        '[INFO] Target architecture: armv7\n'
        '| int x = missing;\n'
        '^^^^^^\n')
assert run(text) == text
annotated = run(text, '--classify').splitlines()
assert [x.split(']', 1)[0] + ']' for x in annotated] == [
    '[progress]', '[progress]', '[progress]', '[error]', '[warning]', '[info]',
    '[unknown]', '[unknown]'], annotated
assert run(text, '--show', 'progress,error,warning') == (
    '[INFO] Downloading module: firmware\nBuilding C object firmware.o\n'
    'Linking C executable firmware\n[ERROR] Downloading failed\n'
    'warning: unused variable\n| int x = missing;\n^^^^^^\n')
assert run(text, '--show=all') == text
assert run(text, '--show', 'unknown') == '| int x = missing;\n^^^^^^\n'
assert run('[INFO] Downloading module: a\n', '-i', '--classify') == ''
assert run('[INFO] Downloading module: a\n', '--show=info') == ''
for sample in ['[ERROR] Building target\n', '\x1b[34m[WARNING] Downloading module\n']:
    assert run(sample, '--classify').startswith(('[error]', '[warning]'))
# Unrecognized/binary/oversized context and synthetic metadata survive any selection.
for sample in ['blorp zqxv unknowntext\n', '| building downloading\n', '^~~~\n',
               '$build = /project/build\n', '[paths]\n',
               '[59/59] [repeat] find (13 lines; like mv)\n',
               'x' * 5000 + '\n', 'bytes\x00data\n', '\n',
               'JSON report saved to: $build/memory_report.json\n',
               '... more output ...\n']:
    assert run(sample, '--show=result') == sample
assert run('Building C object fw.o', '--classify') == '[progress]   Building C object fw.o'
assert run('', '--classify') == ''
# Earlier transformations are consumed as-is; classification adds one prefix only.
compacted = run('Building C object a.o\nBuilding C object b.o\n', '-c')
assert run('Building C object a.o\nBuilding C object b.o\n', '-c', '--classify') == ''.join(
    '[progress]   ' + line for line in compacted.splitlines(keepends=True))
for flags in [('--show=',), ('--show', 'bogus'), ('--show', 'progress,'),
              ('--show', ',error'), ('--show', 'error,,warning')]:
    assert subprocess.run([exe, *flags], input=b'', capture_output=True).returncode != 0
# Completed installation is a result, independently of the package name.
for sample in ['Installed: printf@0.19\n', 'Installed: zqxv@1.2.3\n',
               'Installed: module-name@>=0.3<1.0\n',
               '\x1b[34mInstalled: printf@0.19\x1b[0m\n']:
    assert run(sample, '--classify') == '[result]     ' + sample
    assert run(sample, '--show=result') == sample
    assert run(sample, '--show=info') == ''
assert run('Installing package firmware\n', '--classify') == '[progress]   Installing package firmware\n'
assert run('Failed to install printf@0.19\n', '--classify').startswith(('[error]', '[unknown]'))
assert run('[WARNING] Installed: printf@0.19\n', '--classify').startswith('[warning]')
assert run('Installed package version: 1.2.3\n', '--classify') != (
    '[progress]   Installed package version: 1.2.3\n')
# Terminal build and installation events are results.
for sample in ['[100%] Built target download_ext_modules\n', 'Built target zqxv\n']:
    assert run(sample, '--classify') == '[result]     ' + sample
    assert run(sample, '--show=result') == sample
    assert run(sample, '--show=progress') == ''
# Classification uses the original input even after word-prefix compaction,
# path rewrites and the level2 buffer (which must own a copy of the original).
source = '[100%] Built target download_ext_modules\n[100%] Built target firmware\n'
for flags in [('-c',), ('-cb',), ('-cpb',)]:
    display = run(source, *flags)
    assert run(source, *flags, '--classify') == ''.join(
        '[result]     ' + line for line in display.splitlines(keepends=True))
    assert run(source, *flags, '--show=progress') == ''
buffered = '[INFO] Target architecture: armv7\n[100%] Built target fw\n'
assert run(buffered, '-cb', '--classify') == (
    '[info]       [INFO] Target architecture: armv7\n[result]     [100%] Built target fw\n')
# Tabular measurements are statistics, not command progress percentages.
for sample in ['modules.dmp                          174.66 KB      49.4%\n',
               'newfile.xyz 2.5 MiB 17.3%\n', 'image.bin 512 B 0.5%\n',
               'modules.dmp 174,66 KB 49,4%\n', '/tmp/fw.dmp 174.66 KB 49.4%\n']:
    assert run(sample, '--classify') == '[stats]      ' + sample
    assert run(sample, '--show=stats') == sample
    assert run(sample, '--show=progress') == ''
for sample in ['[49%] Building C object fw.o\n', '[49%] Linking C executable fw\n']:
    assert run(sample, '--classify') == '[progress]   ' + sample
assert run('[ERROR] Memory limit: 100 KB 99%\n', '--classify').startswith('[error]')
assert run('[WARNING] Memory limit: 100 KB 99%\n', '--classify').startswith('[warning]')
# Pure visual separators have a deterministic formatting category, not an
# additional neural output. Text headings and diagnostic marks are excluded.
for sample in ['=' * 60 + '\n', '----------------------\n', '********\n',
               ' \t======== \r\n', '\x1b[34m========\x1b[0m\n', '====']:
    assert run(sample, '--classify') == '[formatting] ' + sample
    assert run(sample, '--show=formatting') == sample
    assert run(sample, '--show=progress') == ''
    assert run(sample) == sample
for sample in ['=== Memory usage ===\n', '--- build failed ---\n', '...\n',
               '^^^^^^\n', '~^~~~~\n', '==\n', '----+----\n', '\n']:
    assert not run(sample, '--classify').startswith('[formatting]')
assert run('Building C object a.o\n========\n', '-cb', '--classify') == (
    '[progress]   Building C object a.o\n[formatting] ========\n')
# Every category uses the same annotation column; preserve original indentation,
# ANSI codes, CRLF and the last line without a newline after that column.
aligned = ('[ERROR] failed\r\nwarning: caution\n'
           '    | source context\n====\n\x1b[34mInstalled: printf@0.19\x1b[0m')
rendered = run(aligned, '--classify').splitlines(keepends=True)
for actual, original in zip(rendered, aligned.splitlines(keepends=True)):
    assert actual[13:] == original
    label, padding = actual[:13].split(']', 1)
    assert label.startswith('[') and padding and not padding.strip()
assert len(rendered) == len(aligned.splitlines(keepends=True))
# Explicit VT100 coloring works in pipes and does not require annotation.
sgr = re.compile(r'\x1b\[[0-9;:]*m')
color_samples = [
    ('Building C object fw.o\n', '\x1b[0;36m'),
    ('[ERROR] failure\n', '\x1b[0;1;31m'),
    ('warning: unused variable\n', '\x1b[0;1;33m'),
    ('[INFO] Target architecture: armv7\n', '\x1b[0;37m'),
    ('Installed: printf@0.19\n', '\x1b[0;1;32m'),
    ('modules.dmp 174.66 KB 49.4%\n', '\x1b[0;35m'),
    ('^^^^\n', '\x1b[0;37m'),
    ('========\n', '\x1b[0;2;37m'),
]
for sample, color in color_samples:
    assert run(sample, '--color') == color + sample[:-1] + '\x1b[0m\n'
    colored = run(sample, '--color', '--classify')
    assert colored.startswith(color) and colored.endswith('\x1b[0m\n')
    assert sgr.sub('', colored) == run(sample, '--classify')
    assert sgr.sub('', colored)[13:] == sample
for sample in ['[ERROR] no newline', '[ERROR] failure\r\n', '\n', 'x\x00y\n']:
    colored = run(sample, '--color')
    assert sgr.sub('', colored) == sample
    suffix = '\r\n' if sample.endswith('\r\n') else '\n' if sample.endswith('\n') else ''
    assert colored.endswith('\x1b[0m' + suffix)
# Category styling replaces source SGR, while cursor controls are retained.
source = '\x1b[34m[ERROR] \x1b[0mproblem\x1b[1m!\x1b[0m\n'
assert run(source, '--color') == '\x1b[0;1;31m[ERROR] problem!\x1b[0m\n'
assert run(source) == source
assert run('\x1b[K[ERROR] failure\n', '--color') == (
    '\x1b[0;1;31m\x1b[K[ERROR] failure\x1b[0m\n')
assert run('Building C object fw.o\n', '--show=result', '--color') == ''
assert run('', '--color') == ''
# Long ls listings are structural, including localized dates, modes and links.
listings = [
    '-rw-r--r--  1 patryk patryk   308 lis  5 16:04 automount.dmfc\n',
    'drwxr-xr-x 2 user group 4096 Nov 5 2025 directory with spaces\n',
    'lrwxrwxrwx 1 user group 4 Nov 5 16:04 link -> target\n',
    '-rwsr-Sr-T+ 1 1000 1000 1.2K Nov 5 16:04 file\n',
    '-rw-r--r--@ 1 user group 308 Nov 5 16:04 file\n',
    '-rw-r--r-- 1 user group 308 2026-11-05 16:04 file\n',
    'crw-rw-rw- 1 root root 1, 3 Nov 5 16:04 null\n',
    '-rw-r--r-- 1 user group 308 lis 5 16:04 error: notes.txt\n',
]
for sample in listings:
    assert run(sample, '--classify') == '[listing]    ' + sample
    assert run(sample, '--show=listing') == sample
    assert run(sample, '--show=progress') == ''
    assert run(sample) == sample
    assert run(sample, '--color') == '\x1b[0;34m' + sample[:-1] + '\x1b[0m\n'
sample = '\x1b[32m' + listings[0].rstrip('\n') + '\x1b[0m\n'
assert run(sample, '--classify') == '[listing]    ' + sample
source = listings[0] + listings[0].replace('automount.dmfc', 'other.dmfc')
display = run(source, '-cb')
assert run(source, '-cb', '--classify') == ''.join(
    '[listing]    ' + line for line in display.splitlines(keepends=True))
for sample in ['-rw-r--r-- comment\n', '-rw-r--r-- 1 u g 308 Nov 5 16:04\n',
               '-rw-r--r-- 1 u g 308 Nov 5 nope file\n',
               '-zzzzzzzzz 1 u g 308 Nov 5 16:04 file\n',
               '[ERROR] -rw-r--r-- 1 u g 308 Nov 5 16:04 file\n']:
    assert not run(sample, '--classify').startswith('[listing]')
# OpenOCD aligns severity labels before ':'. Unknown semantics fall back to
# explicit INFO, while confident semantic progress may refine other INFO logs.
openocd_info = [
    'Info : auto-selecting first available session transport "hla_swd".',
    'Info : The selected transport took over low-level target control.',
    'Info : clock speed 2000 kHz',
    'Info : STLINK V2J37M27 (API v2) VID:PID 0483:374B',
    'Info : Target voltage: 3.208388',
    'Info : [stm32f7x.cpu] Cortex-M7 r0p1 processor detected',
    'Info : [stm32f7x.cpu] target has 8 breakpoints, 4 watchpoints',
    'Info : starting gdb server for stm32f7x.cpu on 3333',
    'Info : Listening on port 3333 for gdb connections',
    'Info : Unable to match requested speed 2000 kHz, using 1800 kHz',
    'Info : device id = 0x10016449',
    'Info : flash size = 1024 KiB',
    'Info : qzxv unrecognized metadata',
]
for sample in openocd_info:
    sample += '\n'
    assert run(sample, '--classify') == '[info]       ' + sample
    assert run(sample, '-i') == ''
warn = 'Warn : [stm32f7x.cpu] Silicon bug: single stepping may enter pending exception handler!\n'
assert run(warn, '--classify') == '[warning]    ' + warn
assert run(warn, '-w') == ''
assert run('Error : connection failed\n', '--classify').startswith('[error]')
# Banners/explicit version reports differ from operations mentioning a version.
for sample in ['Open On-Chip Debugger 0.12.0\n', 'cmake version 3.28.3\n',
               'gcc version 13.3.0\n']:
    assert run(sample, '--classify') == '[version]    ' + sample
    assert run(sample, '--show=version') == sample
    assert run(sample, '--show=progress') == ''
assert run('Building module firmware version 1.2.3\n', '--classify') != (
    '[version]    Building module firmware version 1.2.3\n')
# Independent Python training predictions must match C inference after export.
report = json.loads(Path(reference_path).with_name('report.json').read_text())
data_path = Path(__file__).resolve().parents[1] / 'training/log_categories.tsv'
assert hashlib.sha256(data_path.read_bytes()).hexdigest() == report['dataset_sha256']
reference = json.loads(Path(reference_path).read_text())
texts = [row['text'] for row in reference]
p = subprocess.run([probe], input=('\n'.join(texts)+'\n').encode(), capture_output=True)
assert p.returncode == 0, p.stderr
rows = p.stdout.decode().splitlines()
assert len(rows) == len(reference)
for ref, result in zip(reference, rows):
    scores = [float(x) for x in result.split()]
    assert len(scores) == len(report['classes'])
    assert max(abs(a-b) for a,b in zip(scores, ref['scores'])) < 2e-5, (ref['text'], scores, ref['scores'])
print('Category CLI and Python/C parity checks passed')

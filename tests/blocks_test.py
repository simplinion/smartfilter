"""Online level2 block matching, including level1's compacted output."""
import os
from pathlib import Path
import subprocess
import sys

exe = str(Path(sys.argv[1]).resolve())

def run(text, flags=('-b',)):
    p = subprocess.run([exe, *flags], input=text.encode(), capture_output=True,
                       env={**os.environ, 'LC_ALL': 'C.UTF-8'})
    assert p.returncode == 0, p.stderr
    return p.stdout.decode()


def module(name, index, size, compact=True):
    lines = [
        f'[{index}/59] Downloading module: {name}',
        f'[INFO] Found: {name}@0.19 at $v0_19/dmell-v0.19-armv7-cortex-m7.zip',
        f'[INFO] Downloaded: $ext/{name}-0.19.zip (111620 bytes)',
        f'[INFO] Found dependencies file: {name}.dmd',
        f'[INFO] Selected module file: {name}.dmfc',
        f'[INFO] Dependencies file copied to: $ext/{name}.dmd',
        f'[INFO] Package contents copied to: $ext/{name}',
        f'[INFO] Module installed to: $bin_2/{name}.dmfc',
        f'[INFO] Module loaded: {name} Size: {size} B',
        f"[INFO] To debug module '{name}' in gdb: add-symbol-file <MODULE_ELF_FILE> 0x{size:x}",
        f'[INFO] Processing dependencies for {name}',
        f'[INFO] Processing dependencies from: $ext/{name}.dmd',
        f'Installed: {name}@0.19',
    ]
    if compact:
        for i in range(2, 12):
            lines[i] = '     ↳ ' + lines[i][7:]
        lines[8] = f'            ↳ loaded: {name} Size: {size} B'
        lines[11] = f'                             ↳ from: $ext/{name}.dmd'
    return '\n'.join(lines) + '\n'


first = module('mv', 58, 2758)
second = module('find', 59, 2942)
assert run(first + second) == first + '[59/59] [repeat] find (13 lines; like mv)\n'
assert run(first + second, ()) == first + second
assert run('header\n' + first + second + 'footer\n') == (
    'header\n' + first + '[59/59] [repeat] find (13 lines; like mv)\nfooter\n')
third = module('cat', 60, 3000)
assert run(first + second + third) == (
    first + '[59/59] [repeat] find (13 lines; like mv)\n[60/59] [repeat] cat (13 lines; like mv)\n')
# No assumptions about known command names: generic blocks work as well.
def task(name):
    return f'Start task {name}\nWriting result for {name}\nDone task {name}\n'
assert run(task('alpha') + task('beta')) == task('alpha') + '[repeat] beta (3 similar lines)\n'
assert run(first) == first
assert run(first + second.rsplit('\n', 2)[0] + '\n') == first + second.rsplit('\n', 2)[0] + '\n'
assert run(first + second.rstrip('\n')) == first + '[59/59] [repeat] find (13 lines; like mv)\n'
# Change in the sequence, action, or module binding must keep the details.
for changed in (second.replace('Selected module', 'Skipped module'),
                second.replace('Selected module file: find', 'Selected module file: other'),
                second.replace('Selected module file: find', 'Selected module file: mv'),
                second.replace('Downloading module:', 'Skipping module:'),
                second.replace('     ↳ Selected module file: find.dmfc\n', '')):
    out = run(first + changed)
    assert '[repeat]' not in out, out
    assert out == first + changed
for severity in ('WARNING', 'ERROR', 'FAILED', 'FATAL', 'Exception'):
    changed = second.replace('Selected module file:', severity + ':')
    out = run(first + changed)
    assert out == first + changed
# Incomplete subsequent repetition must flush unchanged at EOF.
partial = '\n'.join(third.splitlines()[:6]) + '\n'
assert run(first + second + partial) == first + '[59/59] [repeat] find (13 lines; like mv)\n' + partial
# Large/unsafe input is a boundary, without dropping bytes.
huge = 'x' * (1024 * 1024 + 1) + '\n'
assert run(first + huge + second) == first + huge + '[59/59] [repeat] find (13 lines; like mv)\n'
assert run('') == ''
assert run('a\x00b\n') == 'a\x00b\n'
# ANSI colors are ignored for comparison; the first block remains byte-exact.
colored = lambda text: ''.join('\x1b[34m' + line + '\x1b[0m\n' for line in text.splitlines())
assert run(colored(first) + colored(second)) == colored(first) + '[59/59] [repeat] find (13 lines; like mv)\n'
# Consume actual output of level1, not reconstructed source lines.
raw = module('mv', 58, 2758, False) + module('find', 59, 2942, False)
compacted = run(raw, ('-c',))
expected_first = run(module('mv', 58, 2758, False), ('-c',))
assert run(compacted) == expected_first + '[59/59] [repeat] find (13 lines; like mv)\n'
assert run(raw, ('-cb',)) == expected_first + '[59/59] [repeat] find (13 lines; like mv)\n'
# A long stream never needs an unbounded whole-log buffer.
plain = ''.join(f'unique message {i}\n' for i in range(1000))
out = run(plain)
assert out == plain
numbered = 'Start task 1\nWriting task 1\nDone task 1\n'
assert run(numbered + numbered.replace('1', '2')) == numbered + '[repeat] 2 (3 similar lines)\n'
# Remember multiple complete variants across unrelated operations.
variant = module('other', 60, 4000).replace(
    '     ↳ Selected module file: other.dmfc\n',
    '     ↳ Selected module file: other.dmfc\n     ↳ Verified signature: valid\n')
assert run(first + variant + second) == (
    first + variant + '[59/59] [repeat] find (13 lines; like mv)\n')
# Versions with a different number of components remain values, not structure.
versioned = second.replace('0.19', '1.2.3')
assert run(first + versioned) == first + '[59/59] [repeat] find (13 lines; like mv)\n'
# Never call an arbitrary middle slice a whole repeated operation. Nested
# dependencies have no top-level progress header and belong to their parent.
nested = module('child', 1, 1200).splitlines()[1:]
parent = first.replace('Installed: mv@0.19\n', '\n'.join(nested) + '\nInstalled: mv@0.19\n')
assert run(parent + second) == parent + second
# The matching first name must not be found merely inside a child's name.
partial = first.replace('Installed: mv@0.19', 'Installed: mv_extra@0.19')
assert run(first + partial) == first + partial
partial = first.replace('Installed: mv@0.19', 'Installed: lib-mv@0.19')
assert run(first + partial) == first + partial
# An error disables collapse of the complete operation, including its suffix.
error = second.replace('     ↳ Selected module file:', 'ERROR: Selected module file:')
assert run(first + error + third) == (
    first + error + '[60/59] [repeat] cat (13 lines; like mv)\n')
# A partial frame at the configured line bound must not seed a tail pattern.
large = first.replace('Installed: mv@0.19\n',
                      ''.join(f'Extra step {i}\n' for i in range(150)) + 'Installed: mv@0.19\n')
assert run(large + second) == large + second
# Cache eviction keeps the oldest unmatched variant visible rather than using
# a stale or freed template; ASan covers ownership while cycling the cache.
variants = [f'[{i + 1}/20] Start task: item{i}\nUnique action{i}\nInstalled: item{i}\n'
            for i in range(20)]
stream = ''.join(variants) + variants[0]
assert run(stream) == stream
print('Block CLI checks passed')

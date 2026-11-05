"""Independent level0 suppression and ordering before other filters."""
import subprocess
import sys

exe = sys.argv[1]


def check(data, expected, *flags):
    if isinstance(data, str):
        data = data.encode()
        expected = expected.encode()
    p = subprocess.run([exe, *flags], input=data, capture_output=True)
    assert p.returncode == 0, p.stderr
    assert p.stdout == expected, (flags, p.stdout, expected)


lines = ['[VERBOSE] detail\n', '[INFO] status\n', '[ERROR] failure\n',
         '[WARNING] warning\n', 'untyped\n', '[DEBUG] detail\n']
text = ''.join(lines)
check(text, text)
for flag, index in [('-v', 0), ('-i', 1), ('-e', 2), ('-w', 3)]:
    check(text, ''.join(line for i, line in enumerate(lines) if i != index), flag)
check(text, 'untyped\n[DEBUG] detail\n', '-view')
check(text, ''.join(lines[2:]), '-v', '-i')
for label, flag in [('INFO', '-i'), ('verbose', '-v'), ('warn', '-w'),
                    ('WARNING', '-w'), ('Error', '-e'), ('ERR', '-e')]:
    check('[' + label + '] x\nkeep\n', 'keep\n', flag)
    check(label + ': x\nkeep\n', 'keep\n', flag)
    check(label + ' : x\nkeep\n', 'keep\n', flag)
    check(label + '\t\x1b[34m \x1b[0m: x\nkeep\n', 'keep\n', flag)
    check('\x1b[0m \x1b[34m[' + label + '] x\n\x1b[0mkeep\n',
          '\x1b[0mkeep\n', flag)
check('[IN\x1b[34mFO\x1b[0m] hidden\n', '', '-i')
check('2026-11-05T12:00:00 [INFO] hidden\n', '', '-i')
check('/project/main.c:12:3: warning: unused\n', '', '-w')
check('/project/main.c:12:3: fatal error: missing\n', '', '-e')
# First explicit severity wins: payload text is not a second severity.
check('[INFO] message mentions error: code\n', '[INFO] message mentions error: code\n', '-e')
check('[ERROR] message mentions info: details\n', '[ERROR] message mentions info: details\n', '-i')
unknown = ('information about warnings\nverbose details\n[INFORMATION] x\n'
           '/tmp/error.log\nhttps://example.com/warning:123\n'
           '[INFO_EXTRA] x\n[ERROR123] x\nkeep\n')
check(unknown, unknown, '-view')
check(b'plain\x00bytes\n[INFO] hidden\r\nlast', b'plain\x00bytes\nlast', '-i')
check('[INFO] no newline', '', '-i')
check('', '', '-i')
# Removed info must never affect compacting, path discovery or block matching.
check('prefix-one\n[INFO] unrelated\nprefix-two\n',
      'prefix-one\n     ↳ two\n', '-ic')
check('[INFO] /opt/sdk/include/a.h\n/opt/sdk/include/b.h\n',
      '/opt/sdk/include/b.h\n', '-iP')
block = ('[1/2] Downloading module: alpha\n[INFO] Working for alpha\nInstalled: alpha@1.0\n'
         '[2/2] Downloading module: beta\n[INFO] Working for beta\nInstalled: beta@1.0\n')
check(block, ('[1/2] Downloading module: alpha\nInstalled: alpha@1.0\n'
              '[2/2] Downloading module: beta\nInstalled: beta@1.0\n'), '-ib')
check('Info \n: keep\n', 'Info \n: keep\n', '-i')
check('Info details: keep\n', 'Info details: keep\n', '-i')
print('Severity CLI checks passed')

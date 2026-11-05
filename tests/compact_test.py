"""CLI regression checks; pass the smartfilter executable as argv[1]."""
import os
import subprocess
import sys


def check(data, expected, flags=('-c',)):
    result = subprocess.run([sys.argv[1], *flags], input=data.encode(),
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            env={**os.environ, 'LC_ALL': 'C.UTF-8'})
    assert result.returncode == 0, result.stderr
    assert result.stdout == expected.encode(), (result.stdout, expected.encode())


check('prefix-one\nprefix-two\nprefix-three\n',
      'prefix-one\n     ↳ two\n      ↳ hree\n')
check('abcd123\nabcd\nxyz\n', 'abcd123\n  ↳ \nxyz\n')
check('same\nsame\n', 'same\n  ↳ \n')
check('abcX\nabcY\n', 'abcX\nabcY\n')
check('prefix-a\nprefix-b', 'prefix-a\n     ↳ b')
check('prefix-a\n\nprefix-b\n', 'prefix-a\n\nprefix-b\n')
check('prefix-a\r\nprefix-b\r\n', 'prefix-a\r\n     ↳ b\r\n')
check('prefix-a\nprefix-a\nprefix-b\n', 'prefix-a\n     ↳ b\n', ('-dc',))
check('prefix-a\nprefix-b\n', 'prefix-a\nprefix-b\n', ())
check('test ą\ntest ć\n', 'test ą\n   ↳ ć\n')
check('界界a\n界界b\n', '界界a\n  ↳ b\n')
check('abcd\ta\nabcd\tb\n', 'abcd\ta\n      ↳ b\n')
check('\x1b[32mprefix-a\n\x1b[32mprefix-b\n',
      '\x1b[32mprefix-a\n\x1b[32m     ↳ b\n')
long_prefix = 'x' * 100000
check(long_prefix + 'a\n' + long_prefix + 'b\n',
      long_prefix + 'a\n' + ' ' * 99998 + '↳ b\n')

check('\x1b[34;1m[INFO]   Installing resource \'dmfc\': x\n\x1b[0m'
      '\x1b[34;1m[INFO]   Installing resource \'dmd\': y\n\x1b[0m',
      '\x1b[34;1m[INFO]   Installing resource \'dmfc\': x\n'
      '\x1b[0m\x1b[34;1m' + ' ' * 30 + "↳ d': y\n\x1b[0m", ('-dc',))
check('\x1b[2Jprefix-a\n\x1b[2Jprefix-b\n',
      '\x1b[2Jprefix-a\n\x1b[2Jprefix-b\n')

print('Compaction CLI checks passed')

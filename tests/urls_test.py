"""URL shortening in level1, independently of file path shortening."""
from pathlib import Path
import os
import subprocess
import sys

exe = str(Path(sys.argv[1]).resolve())

def run(text, flags=('-u',)):
    p = subprocess.run([exe, *flags], input=text.encode(), capture_output=True,
                       env={**os.environ, 'LC_ALL': 'C.UTF-8'})
    assert p.returncode == 0, p.stderr
    return p.stdout.decode()


def footer(*entries):
    return '\n[paths]\n' + ''.join(f'${name} = {value}\n' for name, value in entries)


base = 'https://example.com/packages'
text = base + '/a.tar.gz\n' + base + '/b.tar.gz\n'
expected = base + '/a.tar.gz\n$packages/b.tar.gz\n'
assert run(text) == expected
assert run(text, ()) == text
assert run(text, ('-p',)) == text
assert run(text, ('-P',)) == text
assert run(text, ('-uP',)) == expected + footer(('packages', base))
assert run(text.splitlines()[0] + '\n') == text.splitlines()[0] + '\n'
assert run(text + text.splitlines()[0] + '\n', ('-du',)) == expected
assert run(text + base + '/c.tar.gz\n', ('-cu',)) == expected + '        ↳ c.tar.gz\n'

# Different directories reveal the origin, but different schemes/hosts/ports
# never share an alias just because their URL path components match.
text = 'https://example.com/one/a\nhttps://example.com/two/b\n'
assert run(text, ('-uP',)) == (
    'https://example.com/one/a\n$example_com/two/b\n' +
    footer(('example_com', 'https://example.com')))
text = 'https://example.com/one/a\nhttp://example.com/one/b\nhttps://other.com/one/c\n'
assert run(text, ('-uP',)) == text
text = 'https://example.com:443/one/a\nhttps://example.com:444/one/b\n'
assert run(text) == text

# Queries, fragments, encoding, IPv6 authority and ANSI colors stay intact.
text = (base + '/a%20b?q=a/b&x=1#top\n' + base + '/c%2Fd?q=c/d&x=2#end\n')
assert run(text) == base + '/a%20b?q=a/b&x=1#top\n$packages/c%2Fd?q=c/d&x=2#end\n'
text = 'https://example.com?q=a/b\nhttps://example.com?q=c/d\n'
assert run(text) == 'https://example.com?q=a/b\n$example_com?q=c/d\n'
base6 = 'https://[2001:db8::1]:8443/packages'
text = base6 + '/a\n' + base6 + '/b\n'
assert run(text, ('-uP',)) == base6 + '/a\n$packages/b\n' + footer(('packages', base6))
text = '\x1b[34m' + base + '/a\x1b[0m\n"' + base + '/b".\n'
assert run(text) == '\x1b[34m' + base + '/a\x1b[0m\n"$packages/b".\n'
text = '(' + base + '/a), (' + base + '/b).\n'
assert run(text) == '(' + base + '/a), ($packages/b).\n'

# Segments are matched as complete components. '..' has URI semantics and
# is left opaque; query/fragment slashes never create directory candidates.
text = base + '/a\n' + base + '-extra/b\n'
assert run(text) == base + '/a\n$example_com/packages-extra/b\n'
text = 'https://example.com/a/../packages/one\nhttps://example.com/a/../packages/two\n'
assert run(text, ('-uP',)).endswith(footer(('packages', 'https://example.com/a/../packages')))
for text in ('http:///bad\nhttp:///bad\n', 'ftp://host/path/a\nftp://host/path/b\n'):
    assert run(text) == text

# Files and URLs share a collision-safe namespace but have separate matching.
text = '/opt/toolchain/packages/a\n/opt/toolchain/packages/b\n' + base + '/a\n' + base + '/b\n'
out = run(text, ('-puP',))
assert '$packages/b\n' in out and '$packages_2/b\n' in out
assert '$packages = /opt/toolchain/packages\n' in out
assert '$packages_2 = ' + base + '\n' in out

# Definitions refer to shorter named parents; no dictionary without -P.
text = ('https://example.com/a\nhttps://example.com/b\n' + base + '/a\n' + base + '/b\n')
out = run(text, ('-uP',))
assert out.endswith(footer(('example_com', 'https://example.com'),
                           ('packages', '$example_com/packages')))
assert '[paths]' not in run(text)
assert all(len(new) <= len(old) for old, new in zip(text.splitlines(), run(text).splitlines()))
text = '"' + base + '/a?q=(a,b)."\n"' + base + '/b?q=(c,d)."\n'
assert run(text) == '"' + base + '/a?q=(a,b)."\n"$packages/b?q=(c,d)."\n'
print('URL CLI checks passed')

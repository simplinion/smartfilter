"""Streaming repeated-directory substitution regressions."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='smartfilter-paths-') as root:
    cwd = str(Path(root) / 'working directory')
    Path(cwd).mkdir()
    env = {**os.environ, 'PWD': '/incorrect/pwd', 'LC_ALL': 'C.UTF-8'}

    def run(data, flags=('-p',), report=True):
        report_flags = ['-P'] if report and any('p' in flag for flag in flags) else []
        p = subprocess.run([exe, *flags, *report_flags], input=data.encode(), cwd=cwd, env=env,
                           capture_output=True)
        assert p.returncode == 0, p.stderr
        return p.stdout.decode()

    def footer(*entries):
        return '\n[paths]\n' + ''.join(f'${name} = {value}\n' for name, value in entries)

    assert run('open "' + cwd + '/file.c"\n') == (
        'open "$working_directory/file.c"\n' + footer(('working_directory', cwd)))
    assert run('plain\n') == 'plain\n'
    assert run('') == ''
    single = '/opt/toolchain/include/a.h\n'
    assert run(single) == single  # No variable for a one-off path.
    assert run(single + single, ('-dp',)) == single  # Dedup happens first.
    assert run(single, ()) == single

    text = 'copy /opt/toolchain/include/a.h => /opt/toolchain/include/b.h\n'
    defs = footer(('include', '/opt/toolchain/include'))
    expected = 'copy /opt/toolchain/include/a.h => $include/b.h\n'
    assert run(text) == expected + defs
    assert run(text + text, ('-dcp',)) == expected + defs
    assert run(text.rstrip('\n')) == expected.rstrip('\n') + defs
    assert run('\x1b[34m' + text + '\x1b[0m') == '\x1b[34m' + expected + '\x1b[0m' + defs

    # A common ancestor is found even when the immediate parents differ.
    text = '/opt/sdk/include/a.h\n/opt/sdk/lib/b.a\n/opt/sdk/bin/tool\n'
    assert run(text) == ('/opt/sdk/include/a.h\n$sdk/lib/b.a\n$sdk/bin/tool\n' +
                         footer(('sdk', '/opt/sdk')))
    # Component boundaries and no rewriting URL/relative path tokens.
    text = ('/opt/sdk/include/a.h\n/opt/sdk/include/b.h\n'
            'https://host/opt/sdk/include/c.h ./opt/sdk/include/d.h\n')
    assert run(text) == ('/opt/sdk/include/a.h\n$include/b.h\n'
                         'https://host/opt/sdk/include/c.h ./opt/sdk/include/d.h\n' +
                         footer(('include', '/opt/sdk/include')))
    # The longest repeated prefix wins, including nested paths under cwd.
    text = f'"{cwd}/inc/a.h"\n"{cwd}/inc/b.h"\n'
    assert run(text) == ('"$working_directory/inc/a.h"\n"$inc/b.h"\n' +
                         footer(('working_directory', cwd), ('inc', '$working_directory/inc')))
    # Repeated directory names have stable, distinct variables.
    text = ('/alpha/sdk/inc/a.h\n/alpha/sdk/inc/b.h\n'
            '/beta/sdk/inc/a.h\n/beta/sdk/inc/b.h\n/alpha/sdk/inc/c.h\n')
    assert run(text) == ('/alpha/sdk/inc/a.h\n$inc/b.h\n'
                         '/beta/sdk/inc/a.h\n$inc_2/b.h\n$inc/c.h\n' +
                         footer(('inc_2', '/beta/sdk/inc'), ('inc', '/alpha/sdk/inc')))
    text = '"/opt/sdk with spaces/inc/a.h":12:3\n"/opt/sdk with spaces/inc/b.h":13:4\n'
    assert run(text) == ('"/opt/sdk with spaces/inc/a.h":12:3\n"$inc/b.h":13:4\n' +
                         footer(('inc', '/opt/sdk with spaces/inc')))
    # Compaction sees substituted text; the dictionary remains unabridged.
    text = '/opt/sdk/inc/a.h\n/opt/sdk/inc/b.h\n/opt/sdk/inc/c.h\n'
    assert run(text, ('-cp',)) == ('/opt/sdk/inc/a.h\n$inc/b.h\n   ↳ c.h\n' +
                                  footer(('inc', '/opt/sdk/inc')))
    text = ''.join(f'/unique-{i}/include/a.h\n/unique-{i}/include/b.h\n' for i in range(200))
    out = run(text)
    assert '$include_200/b.h\n' in out
    assert '$include_200 = /unique-199/include\n' in out
    # A parent can be named later than the child. Footer must order definitions
    # by dependency and choose the closest available ancestor.
    base = '/data/projects/chocotechnologies/public/dmod-boot'
    text = (base + '/build/a.o\n' + base + '/build/b.o\n' +
            base + '/src/main.c\n')
    assert run(text) == (base + '/build/a.o\n$build/b.o\n$dmod_boot/src/main.c\n' +
                         footer(('dmod_boot', base), ('build', '$dmod_boot/build')))
    # Multiple levels of references; component boundaries prevent false parents.
    text = ('/opt/toolchain/a\n/opt/toolchain/b\n'
            '/opt/toolchain/build/a\n/opt/toolchain/build/b\n'
            '/opt/toolchain/build/inc/a\n/opt/toolchain/build/inc/b\n'
            '/opt/toolchain/build-extra/a\n/opt/toolchain/build-extra/b\n')
    out = run(text)
    assert out.endswith(footer(('toolchain', '/opt/toolchain'),
                               ('build', '$toolchain/build'),
                               ('inc', '$build/inc'),
                               ('build_extra', '$toolchain/build-extra'))), out
    # Equal/longer substitutions are rejected, even after repeated sightings.
    for directory in ('/x', '/long_directory_name', '/1'):
        text = directory + '/a\n' + directory + '/b\n'
        assert run(text) == text

    # A collision suffix can make a new child alias no better than its parent.
    reserve = '/reserve/x/a\n/reserve/x/b\n'
    text = reserve + '/zz/a/first\n/zz/a/second\n/zz/a/x/one\n/zz/a/x/two\n'
    out = run(text)
    assert '$a/x/two\n' in out
    assert '$x_2 =' not in out  # $x_2 and $a/x have equal lengths.

    # A longer new alias is rejected; rejected names do not consume suffixes.
    reserve = ''.join(f'/reserve-{i}/x/a\n/reserve-{i}/x/b\n' for i in range(9))
    text = (reserve + '/zz/a/first\n/zz/a/second\n'
            '/zz/a/x/one\n/zz/a/x/two\n'
            '/another/long/path/x/one\n/another/long/path/x/two\n')
    out = run(text)
    assert '$a/x/two\n' in out  # Shorter than $x_10/two.
    assert '$x_10 = /another/long/path/x\n' in out
    assert '$x_11' not in out

    # A large suffix must not prevent falling back to a shorter ancestor.
    # Also verify each emitted token is strictly shorter than the raw token.
    body = out.split('\n[paths]\n')[0]
    for raw, rendered in zip(text.splitlines(), body.splitlines()):
        assert len(rendered) <= len(raw), (raw, rendered)
    # By default substitutions do not append a dictionary. -P opts in and
    # enables path substitution on its own, independent of argument order.
    text = '/opt/sdk/inc/a.h\n/opt/sdk/inc/b.h\n'
    body = '/opt/sdk/inc/a.h\n$inc/b.h\n'
    assert run(text, report=False) == body
    assert run(text, ('-cp',), report=False) == body
    assert run(text, ('-P',), report=False) == body + footer(('inc', '/opt/sdk/inc'))
    assert run(text, ('-P', '-p'), report=False) == body + footer(('inc', '/opt/sdk/inc'))
    assert run(text, (), report=False) == text
print('Path dictionary CLI checks passed')

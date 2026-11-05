"""Lossless event grouping, boundaries, compiler variants and interleaving."""
import subprocess
import sys

exe = sys.argv[1]

def run(data, *flags):
    if isinstance(data, str):
        data = data.encode()
    p = subprocess.run([exe, *flags], input=data, capture_output=True)
    assert p.returncode == 0, p.stderr
    return p.stdout


def begin(id, kind, file, row, column, function=''):
    suffix = ' function=' + function if function else ''
    return f'[event {id} begin {kind} {file}:{row}:{column}{suffix}]\n'.encode()


def end(id):
    return f'[event {id} end]\n'.encode()


gcc = ("/src/port.c: In function 'scheduler':\n"
       "/src/port.c:315:13: warning: unused variable 'pxVectorTable'\n"
       '  315 | const int pxVectorTable = 0;\n'
       '      |           ^~~~~~~~~~~~~\n')
assert run(gcc) == gcc.encode()
assert run(gcc, '--group-diagnostics') == gcc.encode()
assert run(gcc, '--events') == begin(1, 'warning', '/src/port.c', 315, 13, 'scheduler') + gcc.encode() + end(1)
for flags in [('-c',), ('-cpb',), ('-c', '--classify', '--color')]:
    display = run(gcc, *flags)
    assert run(gcc, *flags, '--group-diagnostics') == display
    events = run(gcc, *flags, '--events')
    assert events == begin(1, 'warning', '/src/port.c', 315, 13, 'scheduler') + display + end(1)
# First/last line, missing newline, binary and oversized payloads are preserved.
for text in [gcc.rstrip('\n'), '', 'plain\x00bytes\n', 'x' * 300000 + '\n',
             "/a.c: In function 'orphan':\n", gcc + 'int unconfirmed;']:
    assert run(text, '--group-diagnostics') == text.encode()
# Neither interleaved file headers nor mismatched row numbers justify a link.
first = "/a.c: In function 'a':\n"
second = "/b.c: In function 'b':\n"
message = '/a.c:10:3: warning: x\n'
assert run(first + second + message, '--events') == (
    first.encode() + second.encode() + begin(1, 'warning', '/a.c', 10, 3) + message.encode() + end(1))
wrong = gcc + '  400 | other();\n      | ^~~\n'
assert run(wrong, '--events') == begin(1, 'warning', '/src/port.c', 315, 13, 'scheduler') + gcc.encode() + end(1) + b'  400 | other();\n      | ^~~\n'
# A Clang excerpt is associated only when an immediately following caret
# confirms its column. Candidate text is buffered and copied before reuse.
clang = '/src/main.c:9:5: error: missing symbol\n    foo();\n    ^~~\n'
assert run(clang, '--events') == begin(1, 'error', '/src/main.c', 9, 5) + clang.encode() + end(1)
unconfirmed = '/src/main.c:9:5: error: missing symbol\n    foo();\n      ^~~\n'
assert run(unconfirmed, '--events') == (begin(1, 'error', '/src/main.c', 9, 5) +
    b'/src/main.c:9:5: error: missing symbol\n' + end(1) + b'    foo();\n      ^~~\n')
# A diagnostic with another file/location starts its own event, including notes.
a = '/a.c:1:1: error: bad\n'
b = '/b.h:3:2: note: declared here\n'
assert run(a + b, '--events') == begin(1, 'error', '/a.c', 1, 1) + a.encode() + end(1) + begin(2, 'note', '/b.h', 3, 2) + b.encode() + end(2)
assert run('C:\\src\\a.c:7:2: fatal error: header.h missing\n', '--events').startswith(
    begin(1, 'error', 'C:\\src\\a.c', 7, 2))
# Styles affect only the display, not location parsing.
colored = ''.join('\x1b[33m' + line + '\x1b[0m\n' for line in gcc.splitlines())
assert run(colored, '--group-diagnostics') == colored.encode()
assert b'begin warning /src/port.c:315:13 function=scheduler' in run(colored, '--events')
# Repeated prefixes do not grow the pending buffer indefinitely; nothing is lost.
headers = ''.join(f"/src/a.c: In function 'f{i}':\n" for i in range(1000))
assert run(headers, '--group-diagnostics') == headers.encode()
assert run(gcc + '[100%] Built target firmware\n', '--group-diagnostics') == (gcc + '[100%] Built target firmware\n').encode()
# Empty compiler gutters may precede the numbered source; table rows are not
# diagnostic underline gutters even when directly following a source excerpt.
with_gutter = gcc.replace('  315 |', '      |\n  315 |')
assert run(with_gutter, '--events') == begin(1, 'warning', '/src/port.c', 315, 13, 'scheduler') + with_gutter.encode() + end(1)
table = '| File | RAM | ROM |\n'
assert run(gcc + table, '--events') == begin(1, 'warning', '/src/port.c', 315, 13, 'scheduler') + gcc.encode() + end(1) + table.encode()
# Compiler-like strings inside numbered source are source, never a second
# error/location parsed out of the quoted payload.
literal = gcc.replace('  315 | const int pxVectorTable = 0;',
                      '  315 | puts("other.c:9:1: error: literal message");')
assert run(literal, '--events') == begin(1, 'warning', '/src/port.c', 315, 13, 'scheduler') + literal.encode() + end(1)
print('Event CLI checks passed')

"""Give the setjmp calls of Ocarina of Time 1.1.1's main a host setjmp too (runtime/dispatch.c, "guest setjmp/longjmp").

Run after recomp/gen.py, before tools/build_game.py:

    python tools/oot_setjmp_lift.py generated/oot111/main

After each call of the setjmp stub the lifted caller also takes a host setjmp. When a guest longjmp comes back there, the
guest registers are in Cpu, and the caller's locals are reloaded from it. The generated C is game-derived and stays under
generated/. A second run changes nothing. It stops unless it finds exactly the expected return points, so a different
game version can't be edited blindly.
"""
import re
import sys
from pathlib import Path

STUB = 'f_7003c44140(c);'
RETURNS = {0x700165ab04, 0x7001661690, 0x7002365274}   # also OOT_SETJMP_RETURNS in runtime/dispatch.c
HEADER = '#include "cpu.h"\n'
ADD = '#include "cpu.h"\n#include <setjmp.h>\nvoid *rt_setjmp_push(Cpu *c);\n'
MARK = 'rt_setjmp_push(c)'
LOCAL = re.compile(r'\b(x\d+) = c->x\[(\d+)\]')


def reload_for(lines, at):
    """The reload of the enclosing function's register locals: its declarations follow the function's opening line."""
    i = at
    while i >= 0 and not re.match(r'void f_[0-9a-f]+\(Cpu \*c\) \{', lines[i]):
        i -= 1
    if i < 0:
        sys.exit(f'line {at + 1}: no enclosing function')
    parts = []
    for line in lines[i + 1:i + 6]:
        if line.startswith('    uint64_t ') and ' = c->' in line:
            parts += [f'{name} = c->x[{num}];' for name, num in LOCAL.findall(line)]
            if 'sp_ = c->sp' in line:
                parts.append('sp_ = c->sp;')
    return ' '.join(parts)


def main():
    folder = Path(sys.argv[1])
    seen = set()
    for p in sorted(folder.glob('*.c')):
        text = p.read_text(encoding='utf-8')
        if STUB not in text:
            continue
        lines = text.split('\n')
        changed = False
        for n, line in enumerate(lines):
            if STUB not in line:
                continue
            m = re.search(r'(?:x30|x\[30\]) = (0x[0-9a-f]+)ull;', line)
            if not m or int(m.group(1), 16) not in RETURNS:
                sys.exit(f'{p.name}:{n + 1}: an unexpected setjmp call')
            seen.add(int(m.group(1), 16))
            if MARK in line:
                continue
            lines[n] = line.replace(STUB, f'{STUB} if (setjmp(*(jmp_buf *){MARK})) {{ {reload_for(lines, n)} }}')
            changed = True
        if changed:
            out = '\n'.join(lines)
            if out.count(HEADER) != 1:
                sys.exit(f'{p.name}: no cpu.h include to extend')
            p.write_text(out.replace(HEADER, ADD), encoding='utf-8', newline='\n')
            print(f'{p.name}: edited')
    if seen != RETURNS:
        sys.exit(f'return points found: {sorted(map(hex, seen))}')
    print('setjmp calls: ' + ', '.join(sorted(map(hex, seen))))


if __name__ == '__main__':
    main()

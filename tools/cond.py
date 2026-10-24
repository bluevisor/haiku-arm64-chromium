#!/usr/bin/env python3
"""Print the enclosing #if/#elif/#else stack at FILE:LINE (args: path:line ...)."""
import re, sys
for arg in sys.argv[1:]:
    path, ln = arg.rsplit(':', 1); ln = int(ln)
    lines = open(path).read().split('\n')
    stack = []
    i = 0
    while i < ln - 1:
        l = lines[i]
        m = re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b', l)
        if m:
            d = l
            while d.endswith('\\') and i + 1 < len(lines):
                i += 1; d = d[:-1] + lines[i].strip()
            k = m.group(1)
            if k in ('if', 'ifdef', 'ifndef'): stack.append([f'{i+1}: {d.strip()}'])
            elif k in ('elif', 'else') and stack: stack[-1].append(f'{i+1}: {d.strip()}')
            elif k == 'endif' and stack: stack.pop()
        i += 1
    print(f'== {arg}: {lines[ln-1].strip()[:100]}')
    for s in stack:
        print('   ' + ' | '.join(x[:150] for x in s))

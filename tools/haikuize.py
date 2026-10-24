#!/usr/bin/env python3
"""Make preprocessor conditionals treat Haiku as Linux.

Rewrites BUILDFLAG(IS_LINUX) to (BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_HAIKU))
inside #if/#elif directives (including continuation lines) of the given files,
skipping directives that already mention IS_HAIKU. Prints files changed.
"""
import re, sys

PAT = re.compile(r'BUILDFLAG\(IS_LINUX\)')
REPL = '(BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_HAIKU))'

def process(path):
    with open(path) as f:
        lines = f.read().split('\n')
    out, i, changed = [], 0, 0
    while i < len(lines):
        line = lines[i]
        if re.match(r'\s*#\s*(if|elif)\b', line):
            block = [line]
            while block[-1].endswith('\\') and i + 1 < len(lines):
                i += 1
                block.append(lines[i])
            joined = '\n'.join(block)
            if 'IS_HAIKU' not in joined and PAT.search(joined):
                block = [PAT.sub(REPL, b) for b in block]
                changed += 1
            out.extend(block)
        else:
            out.append(line)
        i += 1
    if changed:
        with open(path, 'w') as f:
            f.write('\n'.join(out))
        print(f'{changed:3d} {path}')

for p in sys.argv[1:]:
    process(p)

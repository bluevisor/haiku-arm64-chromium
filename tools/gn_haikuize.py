#!/usr/bin/env python3
"""Add `|| is_haiku` after `is_linux` on the given file:line GN conditions.
Input lines on stdin: path:line:... (git grep -n format)."""
import sys, collections
edits = collections.defaultdict(set)
for l in sys.stdin:
    path, ln, _ = l.split(':', 2)
    edits[path].add(int(ln))
for path, lns in edits.items():
    lines = open(path).read().split('\n')
    n = 0
    for ln in lns:
        s = lines[ln - 1]
        if 'is_haiku' in s or 'is_linux' not in s:
            continue
        lines[ln - 1] = s.replace('is_linux', 'is_linux || is_haiku', 1)
        n += 1
    open(path, 'w').write('\n'.join(lines))
    print(f'{n:3d} {path}')

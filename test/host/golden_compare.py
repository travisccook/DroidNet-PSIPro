#!/usr/bin/env python3
"""Byte-compare two .psig files; on mismatch, print the first divergent
frame (index, t_ms, LED, expected vs actual) instead of a bare cmp fail."""
import sys

REC = 4 + 1 + 48 * 3

def load(p):
    with open(p, 'rb') as f:
        data = f.read()
    i = data.index(b'FRAMES\n') + len(b'FRAMES\n')
    return data[:i], data[i:]

def main(a, b):
    ha, fa = load(a)
    hb, fb = load(b)
    if ha != hb:
        print(f'HEADER differs:\n  {ha!r}\n  {hb!r}'); return 1
    if fa == fb:
        return 0
    n = min(len(fa), len(fb))
    for off in range(0, n, REC):
        ra, rb = fa[off:off+REC], fb[off:off+REC]
        if ra != rb:
            idx = off // REC
            t = int.from_bytes(ra[0:4], 'little')
            for j in range(REC):
                if j < len(ra) and j < len(rb) and ra[j] != rb[j]:
                    what = 't_ms' if j < 4 else 'scale' if j == 4 else f'led[{(j-5)//3}].{"rgb"[(j-5)%3]}'
                    print(f'frame {idx} (t={t}ms): {what} expected {ra[j]} got {rb[j]}')
                    return 1
    print(f'frame count differs: {len(fa)//REC} vs {len(fb)//REC}')
    return 1

if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))

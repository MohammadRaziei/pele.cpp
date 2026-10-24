#!/usr/bin/env python3
"""Preview helper: resolve CSS var() (novasvg lacks support) and rasterize. Not part of pele.cpp."""
import re, sys, novasvg

def resolve(svg: str) -> str:
    m = re.search(r'<svg[^>]*?style="([^"]*)"', svg)
    props = {}
    if m:
        for k, v in re.findall(r'(--[\w-]+):([^;]+)', m.group(1)):
            props[k] = v.strip()
    def val(name, depth=0):
        v = props.get(name)
        if v is None or depth > 8: return None
        mm = re.fullmatch(r'var\((--[\w-]+)(?:,\s*(.*))?\)', v)
        if mm:
            return mm.group(2) if mm.group(2) else val(mm.group(1), depth + 1)
        return v
    def sub(mm):
        r = val(mm.group(1))
        return r if r is not None else (mm.group(2) or '#000')
    out = re.sub(r'var\((--[\w-]+)(?:,\s*([^)]*))?\)', sub, svg)
    out = re.sub(r'style="rx:([\d.]+)(?:px)?"', r'rx="\1"', out)
    return out

if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    width = int(sys.argv[3]) if len(sys.argv) > 3 else 700
    s = open(src, encoding='utf-8').read()
    tmp = dst + '.resolved.svg'
    open(tmp, 'w', encoding='utf-8').write(resolve(s))
    novasvg.svg2png(tmp, dst, width=width, background_color=0xFFFFFFFF)

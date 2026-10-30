# Generates the data in src/drawing_r2010.cpp (between the "generated" markers): AutoCAD's
# VISUALSTYLE records of a new drawing saved as 2010 — the groups after each record's subclass
# marker, its XDATA, and the data of the XRECORD in its extension dictionary
# (ACAD_XREC_ROUNDTRIP) — from jscad's empty2010.dxf (AutoCAD 2017, pinned in
# test/data/manifest.cmake). The materials are the same as in 2007 (drawing_r2007.cpp).
# Plan phase 4, B45.
# Run: py -I tools/gen_visual_styles_2010.py build/test-data/jscad/empty2010.dxf > data.inc
import sys


def records(path):
    data = open(path, 'rb').read().decode('utf-8')
    lines = data.replace('\r\n', '\n').split('\n')
    out = []
    for i in range(0, len(lines) - 1, 2):
        code, value = int(lines[i]), lines[i + 1]
        if code == 0:
            out.append([value, []])
        elif out:
            out[-1][1].append((code, value))
    return out


def body(groups, marker):
    # The groups after the subclass marker, and the XDATA at the end (from a 1001 on)
    i = next(k for k, (c, v) in enumerate(groups) if c == 100 and v == marker)
    rest = groups[i + 1:]
    x = len(rest)
    while x > 0 and rest[x - 1][0] >= 1000:
        x -= 1
    while x < len(rest) and rest[x][0] != 1001:
        x += 1
    return rest[:x], rest[x:]


def c_string(v):
    return '"' + v.replace('\\', '\\\\').replace('"', '\\"') + '"'


def array(name, groups):
    lines = ['const DataGroup %s[] = {' % name]
    for c, v in groups:
        lines.append('    {%d, %s},' % (c, c_string(v.strip())))
    lines.append('};')
    return '\n'.join(lines)


def main():
    recs = records(sys.argv[1])
    by_handle = {}
    for t, groups in recs:
        for c, v in groups:
            if c in (5, 105):
                by_handle[v] = (t, groups)
                break
    styles = []
    for t, groups in recs:
        if t == 'VISUALSTYLE':
            g, x = body(groups, 'AcDbVisualStyle')
            xdict = next(v for c, v in groups if c == 360)
            dict_groups = by_handle[xdict][1]
            entry = [dict_groups[k + 1][1] for k in range(len(dict_groups) - 1)
                     if dict_groups[k] == (3, 'ACAD_XREC_ROUNDTRIP')]
            xrecord = body(by_handle[entry[0]][1], 'AcDbXrecord')[0]
            styles.append((g, x, xrecord[1:]))      # after its 280 (cloning)
    out = []
    for k, (g, x, r) in enumerate(styles):
        out.append(array('visual_style_2010_%d' % k, g))
        out.append(array('visual_style_2010_xdata_%d' % k, x))
        out.append(array('visual_style_2010_roundtrip_%d' % k, r))
    out.append('const DataRecord visual_styles_2010_records[] = {')
    for k, (g, x, r) in enumerate(styles):
        out.append('    {visual_style_2010_%d, %d, visual_style_2010_xdata_%d, %d, visual_style_2010_roundtrip_%d, %d},'
                   % (k, len(g), k, len(x), k, len(r)))
    out.append('};')
    sys.stdout.write('\n'.join(out) + '\n')


main()

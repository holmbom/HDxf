# Generates the data in src/drawing_r2007.cpp (between the "generated" markers): AutoCAD's
# MATERIAL and VISUALSTYLE records of a new drawing saved as 2007 — the groups after each
# record's subclass marker, its XDATA, and for a visual style the data of the XRECORD in its
# extension dictionary (ACAD_XREC_ROUNDTRIP) — from jscad's empty2007.dxf (AutoCAD 2017,
# pinned in test/data/manifest.cmake). Plan phase 3, B34.
# Run: py -I tools/gen_objects_2007.py build/test-data/jscad/empty2007.dxf > data.inc
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
    materials, styles = [], []
    for t, groups in recs:
        if t == 'MATERIAL':
            g, x = body(groups, 'AcDbMaterial')
            materials.append(g)
        elif t == 'VISUALSTYLE':
            g, x = body(groups, 'AcDbVisualStyle')
            xdict = next(v for c, v in groups if c == 360)
            dict_groups = by_handle[xdict][1]
            entry = [dict_groups[k + 1][1] for k in range(len(dict_groups) - 1)
                     if dict_groups[k] == (3, 'ACAD_XREC_ROUNDTRIP')]
            xrecord = body(by_handle[entry[0]][1], 'AcDbXrecord')[0]
            styles.append((g, x, xrecord[1:]))      # after its 280 (cloning)
    out = ['// MATERIAL ByBlock, ByLayer, Global']
    for k, g in enumerate(materials):
        out.append(array('material_%d' % k, g))
    for k, (g, x, r) in enumerate(styles):
        out.append(array('visual_style_%d' % k, g))
        out.append(array('visual_style_xdata_%d' % k, x))
        out.append(array('visual_style_roundtrip_%d' % k, r))
    out.append('const DataRecord materials[] = {')
    for k, g in enumerate(materials):
        out.append('    {material_%d, %d, nullptr, 0, nullptr, 0},' % (k, len(g)))
    out.append('};')
    out.append('const DataRecord visual_styles[] = {')
    for k, (g, x, r) in enumerate(styles):
        out.append('    {visual_style_%d, %d, visual_style_xdata_%d, %d, visual_style_roundtrip_%d, %d},'
                   % (k, len(g), k, len(x), k, len(r)))
    out.append('};')
    sys.stdout.write('\n'.join(out) + '\n')


main()

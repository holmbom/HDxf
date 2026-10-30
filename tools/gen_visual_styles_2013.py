# Generates the data in src/drawing_r2013.cpp (between the "generated" markers): AutoCAD's
# VISUALSTYLE records of a new drawing saved as 2013 — the groups after each record's subclass
# marker and its XDATA (2013 has no extension dictionary or round-trip XRECORD) — from jscad's
# empty2013.dxf (AutoCAD 2017, pinned in test/data/manifest.cmake). Plan phase 5, B59.
# Run: py -I tools/gen_visual_styles_2013.py build/test-data/jscad/empty2013.dxf > data.inc
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
    styles = []
    for t, groups in records(sys.argv[1]):
        if t == 'VISUALSTYLE':
            styles.append(body(groups, 'AcDbVisualStyle'))
    out = []
    for k, (g, x) in enumerate(styles):
        out.append(array('visual_style_2013_%d' % k, g))
        if x:
            out.append(array('visual_style_2013_xdata_%d' % k, x))
    out.append('const DataRecord visual_styles_2013_records[] = {')
    for k, (g, x) in enumerate(styles):
        xdata = 'visual_style_2013_xdata_%d' % k if x else 'nullptr'
        out.append('    {visual_style_2013_%d, %d, %s, %d, nullptr, 0},' % (k, len(g), xdata, len(x)))
    out.append('};')
    sys.stdout.write('\n'.join(out) + '\n')


main()

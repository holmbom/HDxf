#!/usr/bin/env python3
"""Writes the input drawings of the facit for values beyond 16 bits: a polyface with 40 000 vertices and a table with 40 000 layers.

    python -I tools/gen_int16_limits.py doc/facit/large
    python -I tools/fas9_facit_run.py doc/facit/fas9_int16_polyface.scr --input doc/facit/large/int16_polyface_input.dxf --out doc/facit/large
    python -I tools/fas9_facit_run.py doc/facit/fas9_int16_layers.scr --input doc/facit/large/int16_layers_input.dxf --out doc/facit/large

The files are too large for test/data/synthetic. AutoCAD's core reads them and saves its own form: the facit is what it saved.
"""

import os
import sys

COUNT = 40000


def header():
    return ['  0', 'SECTION', '  2', 'HEADER', '  9', '$ACADVER', '  1', 'AC1009', '  0', 'ENDSEC']


def layers():
    out = header() + ['  0', 'SECTION', '  2', 'TABLES', '  0', 'TABLE', '  2', 'LAYER', ' 70', str(COUNT)]
    for i in range(COUNT):
        out += ['  0', 'LAYER', '  2', 'L%d' % i, ' 70', '0', ' 62', '7', '  6', 'CONTINUOUS']
    out += ['  0', 'ENDTAB', '  0', 'ENDSEC', '  0', 'SECTION', '  2', 'ENTITIES',
            '  0', 'LINE', '  8', 'L%d' % (COUNT - 1), ' 10', '0.0', ' 20', '0.0', ' 30', '0.0', ' 11', '1.0', ' 21', '1.0', ' 31', '0.0',
            '  0', 'ENDSEC', '  0', 'EOF', '']
    return out


def polyface():
    out = header() + ['  0', 'SECTION', '  2', 'ENTITIES',
                      '  0', 'POLYLINE', '  8', '0', ' 66', '1', ' 10', '0.0', ' 20', '0.0', ' 30', '0.0', ' 70', '64', ' 71', str(COUNT), ' 72', '1']
    for i in range(COUNT):
        out += ['  0', 'VERTEX', '  8', '0', ' 10', '%d.0' % i, ' 20', '0.0', ' 30', '0.0', ' 70', '192']
    out += ['  0', 'VERTEX', '  8', '0', ' 10', '0.0', ' 20', '0.0', ' 30', '0.0', ' 70', '128',
            ' 71', str(COUNT), ' 72', str(COUNT - 1), ' 73', str(COUNT - 2),
            '  0', 'SEQEND', '  8', '0', '  0', 'ENDSEC', '  0', 'EOF', '']
    return out


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    os.makedirs(sys.argv[1], exist_ok=True)
    for name, lines in (('int16_polyface_input.dxf', polyface()), ('int16_layers_input.dxf', layers())):
        with open(os.path.join(sys.argv[1], name), 'w', newline='') as f:
            f.write('\r\n'.join(lines))


if __name__ == '__main__':
    main()

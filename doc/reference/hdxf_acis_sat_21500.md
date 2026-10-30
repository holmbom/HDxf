# hdxf_acis: the SAT 21500 that is written

What `libs/hdxf_acis` writes, and where each rule comes from. Sources, in the order they decide:
AutoCAD 2025's own files (`doc/facit/acad_2010_dxf_solids.dxf` and its siblings, made by
`doc/facit/10_solids.scr`: 21 bodies in SAT 21500, the same bodies as SAB 22300 in the 2018 file),
what AutoCAD's core (DWG TrueView 2027, `accoreconsole`) measures of the files the builder wrote
(MASSPROP and AREA, `libs/hdxf_acis/tools/acad_measure.py`), and HDxf's own SAT reader and SAT→SAB
encoder (`src/acis.cpp`, `src/acis_sab.cpp`), which AutoCAD's files already passed. No file the
builder wrote is a facit, and no code of another library was used.

Read a body with `hdxf_acis_inspect_sat <file.dxf|file.sat> [n]`.

## 1. Header

```text
21500 <records> 2 24<pad to 23 characters>
16 Autodesk AutoCAD 20 ASM 230.1.0.65535 NT 0  
<unit> <resabs> <resnor> 
```

* `<records>` counts every record including the `asmheader` (the facit: 8 for a sphere with its
  history attribute, 113 for the box). It is the number of `#`. The builder counts its own.
* The third number is 2 in all 21 bodies of the facit (and 92 of 92 in the earlier survey): the
  `asmheader` and the one `body`. The fourth is 24.
* The first line is 23 characters long in every body; it is padded with spaces. The second line
  has two trailing spaces (the third counted string, the date, is `0` and empty). The third has one.
* The two product strings are the kernel's own (AutoCAD 2025 = ASM 230.1.0.65535); they are not
  derived from 21500, and `asmheader` carries the version without `ASM `: `@13 230.1.0.65535`.
* The SAT has no `End-of-ACIS-data`: the last record's `#` ends it (all 21 bodies).
* The unit is the number of millimetres in a coordinate unit. AutoCAD's 25.4 is the imperial
  template's (`acad.dwt`) that Erik's scripts start from. The builder's coordinates are millimetres
  and it writes 1 (`BuildOptions::header_unit`). Measured: AutoCAD's core reads the coordinates as
  the drawing's units whatever the header says (the facit sphere with 25.4 and with 1, in a drawing with
  `$INSUNITS` 0 and 4: volume 7238.2295 in all four, 4/3 · π · 12³), and HDxf's extents do not use it.
  resabs `1e-6` and resnor `1e-10` are AutoCAD's.

Numbers are the shortest text that reads back as the same double (`std::to_chars`, never the
locale's); AutoCAD writes 19 digits. A negative zero is written `0`.

## 2. Records

Every topology and geometry record starts `<type> $-1 -1 $-1`: the attribute pointer, the tag and
the pattern pointer. The builder makes no attributes: AutoCAD's `persubent-acadSolidHistory-attrib`
is history, not geometry, and the regions in the facit have none. Bodies without them were measured
by AutoCAD's core in every case below.

| record | fields after the head |
|---|---|
| `asmheader` | `$-1 -1 @13 230.1.0.65535` (no pattern pointer) |
| `body` | first lump, wire `$-1`, transform |
| `lump` | next lump, first shell, body |
| `transform` | `$-1 -1`, three rows, translation, scale, `rotate`/`no_rotate`, `no_reflect`, `no_shear` (no pattern pointer) |
| `shell` | next shell, subshell `$-1`, first face, wire `$-1`, lump |
| `face` | next face, first loop, shell, subshell `$-1`, surface, `forward`/`reversed`, `single`, or `double out` |
| `loop` | next loop, first coedge, face |
| `coedge` | next, previous (round the loop), partner, edge, `forward`/`reversed`, loop, pcurve |
| `edge` | start vertex, start parameter, end vertex, end parameter, a coedge, curve, `forward`, `@7 unknown` |
| `vertex` | an edge, 0 or 1, point |
| `point` | x y z |

The field order is HDxf's encoder's field list (`src/acis_sab.cpp`) and was checked against the
facit; the hand reading in the plan's examples (`face $10 -1 $-1 $11 $12 $4 $-1 $13 reversed single`
…) is the same layout. A 21500 coedge has no integer after the loop (that is 21800's).

What the facit settles:

* **Orientation.** A face's normal (the surface's, turned by `reversed`) points out of the material;
  an outer loop runs counter-clockwise about it. The two coedges of an edge run in opposite
  directions and are each other's partners; a lone coedge has partner `$-1`. Edges are always
  `forward` with start parameter < end parameter in AutoCAD's bodies, so the builder normalizes: an
  edge "against its curve" is written from its other vertex and its coedges' senses are exchanged.
* **Sidedness.** A solid's faces are `single`, with no containment word. A REGION's and a sheet's
  are `double out` (the facit's regions; the ACISOUT sheet in `surface_body_probe.sat`).
* **Vertex integer.** 0 for the start of its edge, 1 for the end (every vertex of the box). A closed
  edge's vertex is 0 in the cylinder and 2 in the facit's regions; the builder writes 0 and the core measured
  the circle regions and cylinders right.
* **Convexity.** `unknown` in every plain edge; a `tangent` appears in the loft's seam. The builder
  always writes `unknown`.
* **Lumps and shells.** Two lumps are two `lump` records chained by *next lump*, each with its shell (the
  facit's united boxes). A void shell is a second shell of the lump (measured: a cube with a cube cavity,
  volume 63000, area 10200); its faces are `reversed` against the outward geometry and its loops run the
  other way. Not in the facit: Erik's list below.
* **Record order.** AutoCAD's order is the order a depth-first save meets the records. The builder
  numbers by kind (header records, shells, faces, loops, coedges, edges, vertices, points, surfaces,
  curves, pcurves). HDxf's reader and encoder and AutoCAD's core accept it.
* **Whole sphere, whole torus.** One face with loop `$-1`: no loop, edge, vertex or seam.
  `sphere-surface` is centre, radius, u direction `1 0 0`, pole `0 0 1`; `torus-surface` is centre,
  axis, major, minor, u direction.
* **Cylinder, cone.** `cone-surface`: centre of the reference circle (the middle of the axis), axis,
  major axis (length = the radius there), ratio 1, `I I`, sine and cosine of the half angle, the u scale
  (= the major axis' length in every cone and cylinder of the facit), `forward`, `I I I I`. The wall has
  one loop per circle and **no seam edge**. A cone's apex is a vertex and an *edge without a curve* in
  its own loop: `edge … $v 1 $v 0 $coedge $-1 forward @7 unknown`, one coedge, `reversed`, partner `$-1`.
  The builder writes exactly this and nothing else for a degenerate edge, and only at a cone's apex.
* **Seams on analytic surfaces** are an edge with two coedges in the same loop. Not in the facit.
  Measured: a cylinder whose wall is one loop of circle, seam, circle, seam, with and without
  pcurves in the cylinder's parameters, has the volume and the area of the cylinder in AutoCAD's core.
* **Poles as edges** are *not* accepted: a sphere with a seam and two degenerate pole edges is
  read and has the right volume, but AutoCAD's core measures its area as 0. The builder refuses a
  degenerate edge anywhere but at a cone's apex (`unsupported_geometry`).

## 3. Splines

All forms are AutoCAD 2025's (the 2010 loft of the facit, `exact_int_cur`, `exp_par_cur`; the 2007 loft,
`exact_spl_sur`, whose 21500 form has the logical `F` that HDxf's field list gives it).

* **bs3 / bs2 curve.** `nubs|nurbs degree closure n` then `n` pairs *(knot, multiplicity)* then the
  control points (`x y z`, or `u v`, each followed by its weight for `nurbs`). The knots are the whole
  vector without its **first and last** value, grouped by value (a clamped end of a cubic is
  multiplicity 3, not 4). The control point count follows: sum + 1 - degree. `closure` is
  `open | closed | periodic`.
* **bs3 surface.** `nubs|nurbs du dv [both] closure_u closure_v singularity_u singularity_v nu nv`, the u
  knots, the v knots, then the control net **u fastest** (the facit's lofts: 73 points across, then the next
  row). `both` is the rational word of `nurbs` (HDxf's test_acis.cpp has it). Singularity words:
  `none | low | high | both`.
* **Discontinuities** are three lists, derivative order 1, 2, 3, each a count and the knots. The facit's
  surfaces list every inner knot where the order p − m + 1 (m the multiplicity) is at most 3; a periodic
  direction lists the knot where the period starts too (a cubic: order 3). AutoCAD's kernel finds the *real*
  jumps (its curve with a double inner knot, which can be smooth, lists none); the builder lists every
  knot where a jump is possible, a superset, which only adds break points.
* **`spline-surface`.** `forward { exact_spl_sur 22800 full <bs3 surface> 0 <D u> <D v> F  F 1 F 0 F 1 F 0
  UNEXTENDED } I I I I`: 22800 is the version of the data in AutoCAD 2025's kernel, `0` the fit tolerance
  (exact), `F` the logical that 21500 added, `F 1 F 0` the safe range AutoCAD writes (twice).
* **`intcurve-curve`.** `forward { exact_int_cur 22800 full <bs3 curve> 0 null_surface null_surface nullbs
  nullbs I I <D> 0 F 1 F 0 UNEXTENDED UNEXTENDED } I I`.
* **`pcurve`.** `0 forward { exp_par_cur <bs2 curve> 0 <the face's surface> } 0 0`. A B-spline surface is
  written `spline forward { ref n } I I I I`, `n` the number of its subtype definition among all the
  definitions of the file in order (a `{` not followed by `ref`; the builder writes the surfaces before
  the curves and the pcurves for it); an analytic surface is written whole (`plane …`, `cone …`).
* **The pcurve's parameter.** It is the edge's curve parameter for a coedge written forward and its
  **negation** for one written reversed: AutoCAD's loft has the seam's reversed pcurve on −1 … 0 and
  the circle's on −π … π with the u direction turned; HDxf's reader evaluating the facit's four pcurves
  meets the edges' curves to 6e-4 (the loft's fit tolerance) with this rule and misses by 30 and 40 mm
  without it (`acis_facit_pcurves_have_the_parameter_of_the_edges_curve`). The builder's pcurves are on
  the edge's parameter and the writer turns the reversed coedges'.
* **When a pcurve is needed.** A coedge on a B-spline surface must have one (`pcurve_required`).
  On analytic surfaces none is written unless given (the facit's cylinder, cone and caps have none;
  seams on a cylinder were measured with and without).

Measured with AutoCAD's core: a closed cylinder whose wall is an exact rational NURBS (the circle as
four quadratic arcs, a seam, four pcurves, `exact_int_cur` circles) has volume 7853.9816 and area
2199.1149, as 2500π and 700π, in DXF 2010 and 2018.

## 4. The text

* **Cipher.** Each character from `!` (33) to `~` (126) becomes 159 − c; the space and everything else
  stay. It is its own inverse, and HDxf's `acis_deciphered()` is it. A capital `A` becomes `^`, `@` becomes `_`.
  The `^ ` that AutoCAD's ASCII DXF shows for a caret is **DXF's caret notation** (`^` followed by a space
  is the character `^`), not part of the cipher: HDxf's group writer adds the space and its reader removes it,
  so `AcisData::lines` hold `^` and the builder adds nothing. A character outside printable ASCII is an
  `encoding` error (the builder writes no user strings; the two it writes, `230.1.0.65535` and `unknown`,
  are fixed).
* **Lines.** A record starts a line and continues on lines that start with a space; the builder breaks at
  token boundaries, 254 characters at most. AutoCAD's own lines are up to 254 in the facit and 312 in the
  rotated box. Measured: a 533-character line (HDxf's groups 3 + 1) reads in AutoCAD's core in 2010 and 2018.
* **HDxf's groups for one SAT line of n characters** (n counted as written, without the caret escapes):
  n ≤ 254: one group 1. n = 255: group 3 of 255, group 1 empty. 256: 3 of 255, 1 of one character. 510:
  3, 3 and an empty 1. 511: 3, 3, 1 of one. `AcisData::lines` reads back whole in ASCII and binary DXF
  (`acis_encoding_dxf_line_lengths_254_255_256_510_511`).

## 5. Measurements (AutoCAD's core, DWG TrueView 2027, `X.118.0.0`)

`hdxf_acis_export_cases <dir>` writes 31 cases (20 built, 11 of AutoCAD's own bodies) as DXF 2010 and 2018 (one body to a drawing), among them
the facit's own bodies cut out of `doc/facit` and the builder's; `acad_measure.py <dir>` runs MASSPROP and
AREA on each (the two BODY cases, closed and open, which MASSPROP does not cover, are read by the core with LIST as a BODY). Every row below is the same in 2010 and 2018, and the builder's body equals AutoCAD's own where
the facit has one (box, sphere, cylinder, cone, torus, both regions, the rotated box, and the three bodies of
`26_acis_referens.scr`: the hole block, the cavity solid and the hemisphere; the hole block, the cavity solid and the hemisphere are
built with the geometry around the center and a body transform, as AutoCAD saves them, and equal AutoCAD's own in 2010 and 2018).

| case | volume (area for a region) | surface area (perimeter) | centroid | analytic |
|---|---:|---:|---|---|
| box 20 × 15 × 20 | 6000 | 2000 | 10, 10, 10 | 6000, 2000 |
| sphere r 12 | 7238.2295 | 1809.5574 | 50, 10, 10 | 2304π, 576π |
| cylinder r 10 h 25 | 7853.9816 | 2199.1149 | 90, 10, 12.5 | 2500π, 700π |
| cone r 12 h 30 | 4523.8934 | 1670.4849 | 130, 10, 7.5 | 1440π, 12π(12 + √1044) |
| torus R 15 r 4 | 4737.4101 | 2368.7051 | 170, 10, 12 | 480π², 240π² |
| rectangle region 30 × 20 | 600 | 100 | 15, 70 | |
| circle region r 15 | 706.8583 | 94.2478 | 170, 145 | 225π, 30π |
| rotated box (AutoCAD's rotation) | 6000 | 2000 | 12.7536, 7.2464, 9.2106 | |
| frustum r 12 / 6, h 20 | 5277.8757 | 1746.2575 | 0, 0, −2.1429 | |
| 30 × 30 × 24 with a hole r 6 | 18885.6639 | 5358.5840 | 15, 15, 12 | 21600 − 864π, 4680 + 216π |
| two cubes (two lumps) | 16000 | 4800 | 27.5, 10, 10 | |
| cube 40 with a cavity of 10 | 63000 | 10200 | 20, 20, 20 | |
| cube 40 with a cavity of 10, as AutoCAD saves it (a void shell) | 63000 | 10200 | 120, 20, 20 | AutoCAD's MASSPROP log: 63000 |
| 30 × 30 × 24 with a hole r 6, as AutoCAD saves it | 18885.6639 | 5358.5840 | 15, 15, 12 | AutoCAD's MASSPROP log: 18885.6639 |
| hemisphere r 12 (SLICE of a sphere) | 3619.1147 | 1357.1680 | 200, 20, 24.5 | 1152π, 432π; AutoCAD's MASSPROP log: 3619.1147 |
| NURBS cylinder | 7853.9816 | 2199.1149 | 0, 0, 12.5 | 2500π, 700π |
| cylinder with a seam, with and without pcurves | 7853.9816 | 2199.1149 | 0, 0, 0 | |

MASSPROP prints four decimals; the comparison tolerance is 6e-4 plus 2e-6 of the value (the rounding, plus a
relative part), 2e-3 for the centroid.

## 6. What the facit does not have

`doc/facit/acad_20xx_dxf_acis_reference*.dxf` (script `26_acis_referens.scr`, AutoCAD 2025) holds a block with a through hole made by
SUBTRACT, a solid with a cavity and a sphere cut through its center by SLICE. What they show, and `hdxf_acis` writes the same
forms (tests `acis_facit_reference_bodies_have_the_topology_and_geometry_of_autocads` and the two after it):

* a cavity is a second `shell` of the same `lump`, chained from the outer shell's `next shell` field; its faces' normals (the plane's
  turned by the face's sense) point into the cavity, out of the material. The builder's `ShellRole::Void` writes this;
* a hole's wall is a `cone-surface` (a cylinder) with two loops of one coedge each, one per circle, the circles closed
  `ellipse-curve` edges with one vertex and no seam. AutoCAD's circles run from −π to π, the builder's from 0 to 2π (same circle);
* a cut sphere is a `sphere-surface` face with one loop of one coedge (the rim circle) and a `plane-surface` face with the same
  edge: no seam, no pole vertex, no pole edge. The builder writes it as a face with a `Boundary` loop.

The builder writes these forms from the rules above and AutoCAD's core accepted them, but there is no
AutoCAD-saved file to hold them against:

* a closed solid with a B-spline wall whose exact form is `exact_spl_sur` in SAT 21500 (the facit's 2007 file has
  it in 21200 as a loft; its 2010 file has the procedural `loft_spl_sur`); the script's LOFT step was not made;
* a trimmed B-spline surface with a hole and its pcurves;
* a seam on a cylinder, cone, sphere or torus as AutoCAD writes it (it writes none for the primitives or for the hole's wall
  or the cut sphere; the cylinder cut through its axis that the script header mentions was not made), and
  a pole as AutoCAD writes it (the builder refuses poles as edges; the cut sphere's pole is inside its face);
* SAT strings that are not fixed, and lines of 255 characters or more in a SAT 21500 file of AutoCAD's.

## 7. Limits

* One body per SAT; no wires, no subshells, no tolerant edges or vertices (`tedge-edge` and the others have
  no complete grammar here), no attributes.
* Transforms are a rotation, a translation and a positive uniform scale; a reflection or a shear is an error.
* Cones are circular (ratio 1); the torus is a ring torus (major > minor > 0).
* Planar faces are checked for the direction of their loops; the other faces' loops are checked for closure and
  for the edges' use, not for the direction on the surface. The check that a pcurve meets its edge's curve, and
  that a curve lies on an analytic surface, is numeric (16 and 8 samples) to the tolerance, not a proof for every
  spline surface. A shell's overall direction (the normals in or out) is the caller's: the builder does not
  compute the volume's sign.
* SAT 21500 goes into DXF 2010 as lines and into 2013 (SAB 21800) and 2018 (SAB 22300) through HDxf's encoder.
  Older DXF versions would need their own SAT versions and are not written.

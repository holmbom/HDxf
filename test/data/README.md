# HDxf test data

| Directory | Contents | Checked in |
|---|---|---|
| `synthetic/` | Small files of our own (below) | yes |
| `manifest.cmake` | External files: URL pinned to a commit + SHA256 | yes |
| `${CMAKE_BINARY_DIR}/test-data/` | The external files, downloaded at configure time | no |

`*.dxf` files are compared byte for byte; `.gitattributes` keeps git off their line endings.

## External files

They are not checked in and are not distributed with HDxf. `test/CMakeLists.txt` downloads
each file in `manifest.cmake` when it is missing from the build directory, checks its
SHA256 and stops with an error on a mismatch. `-DHDXF_TEST_DATA=OFF` turns the download
off. Tests whose file is missing are skipped (`SKIP` in the test output), so the build and
the other tests work offline. Tests: `test/test_external_files.cpp`.

| Source | Commit | License |
|---|---|---|
| [ACadSharp](https://github.com/DomCR/ACadSharp) `samples/` | `29137f3adaf75ddebd532179eda1e811525ca574` | MIT |
| [ezdxf](https://github.com/mozman/ezdxf) `integration_tests/data/`, `examples_dxf/`, `examples/acistools/` | `7b202613e0d689955b8932a22bcbcf68bdd684f0` | MIT |
| [jscad sample-files](https://github.com/jscad/sample-files) `dxf/autocad2017/`, `dxf/others/pyramid-base.dxf` | `f6f6485e7286a50c4c304d697be9cc9f7e64beb6` | MIT |
| [Aspose.CAD for .NET](https://github.com/aspose-cad/Aspose.CAD-for-.NET) `Examples/Data/DXF-Drawings/` | `44108cca2375adfd95f1a6f5a9632a952d44f1b2` | MIT |
| [LibreDWG](https://github.com/LibreDWG/libredwg) `test/test-data/` (`example_2007/2010/2013.dxf`, AutoCAD's files; `r13/v.dxf`) | `a8ccb1f314d9feb2215be67d1f6f2a3d64b2c9a0` | GPL-3.0 (test data, only read) |
| [gvSIG SIGA](https://github.com/iCarto/gvsig-siga) `libGDBMS/src/test/resources/dxfprueba.dxf` (AutoCAD R13's file) | `278da442d4cf01bde67b83b6ec0713e1639ccae7` | GPL-3.0 (test resource, only read) |
| [SpatialAuxetic](https://github.com/ChiangYuChou/SpatialAuxetic) `MATLAB/HEAD.DXF`, `simp.dxf` (AutoCAD R14's header, 3DFACE) | `c20201ebd1dab5d25f2c41dfc060b398591c6417` | MIT |

The files are only read by the tests. No code from any of these projects is used in HDxf.

### Facit

Counted from the files themselves with a small script of our own that reads ASCII and
binary group codes directly, not with HDxf. Every binary file gives the same counts as
its ASCII twin.

- **Entities**: top-level records in the ENTITIES section. VERTEX/SEQEND after a POLYLINE
  and ATTRIB/SEQEND after an INSERT with `66 1` belong to their owner and are counted
  separately.
- **Layers**: entries in the LAYER table. **Blocks**: BLOCK records in the BLOCKS section.
- **Texts**: group 1 of TEXT, exact bytes.

#### ACadSharp — one drawing saved by AutoCAD in every version

Saved by AutoCAD (`save_samples.lsp`) as R12, 2000, 2004, 2007, 2010, 2013 and 2018,
ASCII and binary — 14 files, 24 MB.

**R12** — `sample_AC1009_ascii.dxf`, `sample_AC1009_binary.dxf`

| | |
|---|---|
| Entities | 159: 3DFACE 1, ARC 1, CIRCLE 8, DIMENSION 11, INSERT 45, LINE 28, POINT 6, POLYLINE 26, SHAPE 1, SOLID 1, TEXT 29, VIEWPORT 2 |
| Owned | VERTEX 490, ATTRIB 4, SEQEND 28 |
| Layers | 21: `0`, `LAYER1`, `LAYER_OFF`, `LAYER_FREEZE`, `LAYER_LOCK`, `LAYER_NOPLOT`, `LAYER_COLOR_80`, `LAYER_LT_DASH`, `LAYER_LW_035`, `LAYER_TRANSP_70`, `LAYER_VP_FREEZE`, `LAYER_DESC`, `LAYER_TRUE_COLOR`, `LAYER_COLOR_BOOK`, `DEFPOINTS`, `LAYER2`, `LAYER_LW_050`, `APP_LAYER`, `COLOR_125_33_79`, `0___1`, `LAYER_COLOR_80___1` |
| Blocks | 95 (`$MODEL_SPACE`, `$PAPER_SPACE`, …) — AutoCAD explodes the 2000+ objects into anonymous `*U`/`*X` blocks |
| Texts | `Hello this is a single line text` (8×), `XData All`, `Materials`; in BLOCKS `D8 "45,6"` |
| Header | `$ACADVER AC1009`, no `$DWGCODEPAGE`; `$HANDSEED` 2459 (ASCII) / 346B (binary) |

AutoCAD writes ∅ as Ø — byte `0xD8` in ANSI_1252 — in R12. The `94°` of the 2000+ files
is not in the R12 file.

**2000–2018** — `sample_AC1015` … `sample_AC1032`, `_ascii` and `_binary`

| | |
|---|---|
| Entities | 165: 3DFACE 1, 3DSOLID 2, ACAD_TABLE 2, ARC 1, CIRCLE 8, DIMENSION 11, ELLIPSE 1, HATCH 8, IMAGE 1, INSERT 6, LEADER 1, LINE 26, LWPOLYLINE 21, MESH 2, MLINE 3, MTEXT 5, MULTILEADER 15, PDFUNDERLAY 1, POINT 6, POLYLINE 2, RAY 1, REGION 1, SHAPE 1, SOLID 1, SPLINE 2, TEXT 29, TOLERANCE 3, VIEWPORT 2, WIPEOUT 1, XLINE 1 |
| Owned | VERTEX 12, ATTRIB 4, SEQEND 4 |
| Layers | 2000, 2004, 2007: 21 — the 19 below plus `0 @ 1` and `Layer_color_80 @ 1`. 2010, 2013, 2018: 19: `0`, `Layer1`, `Layer_Off`, `Layer_Freeze`, `Layer_Lock`, `Layer_NoPlot`, `Layer_color_80`, `Layer_Lt_dash`, `Layer_lw_035`, `Layer_transp_70`, `Layer_vp_freeze`, `Layer_desc`, `Layer_true_color`, `Layer_color_book`, `Defpoints`, `Layer2`, `Layer_lw_050`, `app_layer`, `color_125_33_79` |
| Blocks | 27 (`*Model_Space`, `*Paper_Space`, `*Paper_Space0`, `*Paper_Space1`, …). Block 20 is `*U17` up to 2007 and `my-dynamic-block` from 2010 |
| Texts | `Hello this is a single line text` (8×), `XData All`, `Materials` |
| Header | `$DWGCODEPAGE ANSI_1252` in all |

Text encoding, seen in the files:

| | 2000 | 2004 | 2007+ |
|---|---|---|---|
| `°` | `94\U+00B0` | `94\U+00B0` | `94\U+00B0` |
| `∅` | `\U+220545,6` | `\U+220545,6` | UTF-8 `E2 88 85` `45,6` |
| MTEXT line break, ASCII | `\P` | `^J` | `^J` |
| MTEXT line break, binary | `\P` | byte `0A` | byte `0A` |

Group code 101 (`Embedded Object`): none up to 2010; 8 in 2013 (ACDSSCHEMA); 14 in 2018
(ACDSSCHEMA, MTEXT, ATTRIB, ATTDEF). In 2018 the multi-line ATTRIB has an empty group 1 —
the text is in its embedded MTEXT. In 2000–2013 `Embedded Object` is only a group 1 text
value (attribute tag `MULTI_LINE_ATT`).

#### jscad — AutoCAD 2017's new drawing as 2007

`jscad/empty2007.dxf` (AC1021, `$ACADMAINTVER 50`): the objects a new 2007 drawing has (plan
phase 3, B34) — MATERIAL `ByBlock`, `ByLayer`, `Global` and 26 VISUALSTYLE, each with an
extension dictionary holding an XRECORD `ACAD_XREC_ROUNDTRIP`; `$CMATERIAL` → `ByLayer`,
`$INTERFEREOBJVS` → `Realistic`, `$INTERFEREVPVS` → `Wireframe`, every layer's `347` →
`Global`, VPORT/VIEWPORT `348` → `2dWireframe`. `src/drawing_r2007.cpp` has these records
(`tools/gen_objects_2007.py`), `test/test_write_2007.cpp` compares them.

#### jscad — AutoCAD 2017's new drawing as 2010

`jscad/empty2010.dxf` (AC1024, `$ACADMAINTVER 225`): the objects a new 2010 drawing has (plan
phase 4, B45) — the same as in `empty2007.dxf` but the 26 VISUALSTYLE in 2010's form (`177`, `291`
first, every property followed by `176 1`) and their round-trip XRECORDs without 2007's
`RTVSPropertyOp…`; no APPID `ACAD_MLEADERVER`; MLEADERSTYLE `Standard` with `179 2` and
`271`–`273`. `src/drawing_r2010.cpp` has the visual styles (`tools/gen_visual_styles_2010.py`),
`test/test_write_2010.cpp` compares them.

#### DXF 2010 (phase 4)

| File | Saved by | What it has |
|---|---|---|
| `libre/Leader_2010.dxf` | AutoCAD (LibreDWG's test drawing, via OpenAEC, MIT), AC1024, `$ACADMAINTVER 226` | The multileader of `Leader_2000` … `_2007` with `270`, `271`–`273`; five IMAGE with `290` |
| `libre/gh209_1_2010.dxf` | AutoCAD (LibreDWG's test drawing gh209_1, via OpenAEC, MIT), AC1024, `$ACADMAINTVER 226` | GEODATA `4` in the 2010 form (version 2): local grid, latitude 37.795, longitude −122.394, no coordinate system |

#### jscad — AutoCAD 2017's new drawing as 2013

`jscad/empty2013.dxf` (AC1027, `$ACADMAINTVER 125`): the objects a new 2013 drawing has (plan
phase 5, B59) — the same as in `empty2010.dxf` but the 26 VISUALSTYLE in 2013's form (`177 3`,
`291`, `70 58`, every property's value followed by `176`) without extension dictionary and
round-trip XRECORD; MLEADERSTYLE `Standard` without extension dictionary, `295 1`, `298 1`; every
LAYER `348 0`; the VPORT table's XDATA `ACAD` `DbSaveVer` `26`; no `AcDsDecomposeData`; ACDSDATA
with `AcDb_Thumbnail_Schema`, the four system schemas and one record (Model's PNG, 1113 bytes);
`$REQUIREDVERSIONS 160 0` after `$LASTSAVEDBY`. `src/drawing_r2013.cpp` has the visual styles
(`tools/gen_visual_styles_2013.py`), `test/test_write_2013.cpp` compares them.

#### DXF 2013 (phase 5)

| File | Saved by | What it has |
|---|---|---|
| `libre/example_2007.dxf`, `_2010` | AutoCAD (LibreDWG's example drawing), AC1021/AC1024, `$ACADMAINTVER` 50/226 | The drawing of `example_2013` in the older form: SAT 21200/21500 in `1`/`3`, the GUIDs as XDATA `ACAD_OBJECT_CHANGE_GUID`, the MULTILEADER's `295 1` and the style's `298 1` in round-trip XRECORDs (step 5.3) |
| `libre/example_2013.dxf` | AutoCAD (LibreDWG's example drawing), AC1027, `$ACADMAINTVER 125` | Three ACIS bodies (REGION `176` with `290 0` and the GUID of zeros; `2E1`, `37D` with GUIDs), SAB in ACDSDATA; MULTILEADER `640` with `295 1`; MLEADERSTYLE with `298 1`; only the dimension style `ISO-25` (no `Standard`) |
| `libre/example_2018.dxf` | AutoCAD (LibreDWG's example drawing), AC1032, `$ACADMAINTVER 90 29` | The drawing of `example_2013` saved as 2018 (phase 6): the same 253 header variables, the three bodies' SAB as 22300/`ASM BinaryFile4`; CRLF line ends in LibreDWG's repository (SHA256 of the LF form 3938567f…2c11, OpenAEC's copy) |
| `ezdxf/mtext_framed_columns.dxf` | ezdxf's integration test data saved by AutoCAD 2013 (AC1027, `$ACADMAINTVER 105`, MIT) | Six MTEXT: a static text with three columns (`2D`, its other columns `71`, `72` linked by `ACAD_MTEXT_COLUMNS`, the text cut after each `\N`, `ACAD_MTEXT_DEFINED_HEIGHT`) and a dynamic one (`33` with `76`, `77` empty), each with a text frame (`90 17`, `ACAD_MTEXT_TEXT_BORDERS` and one LWPOLYLINE for each column) - phase 6, B73/B74 |
| `ezdxf/mtext_text_frame.dxf` | The same, AC1027 | One MTEXT with a text frame alone (`90 16` without `63`/`45`/`441`, `ACAD_MTEXT_TEXT_BORDERS` and its LWPOLYLINE `9F`) |

ACadSharp's `sample_AC1027` (ASCII and binary; `$ACADMAINTVER 125`) is its 2010 drawing saved as
2013: three bodies `D65`, `D69`, `D6A` with GUIDs (`290 1`, `{1a113328-…}` for D65), MLEADERSTYLEs
with `298 0`, every layer `348 0`, ACDSDATA with Thumbnail, ASM and the system schemas. jscad's
`3Ddraw`, `3Dlines`, `3Dmesh01` and Oxcart's four drawings are AutoCAD 2017's 2013 files too. All of
them are written back byte for byte.

#### MULTILEADER (phase 3, B36)

| File | Saved by | Multileaders |
|---|---|---|
| `libre/Leader_2000.dxf`, `_2004`, `_2007` | AutoCAD (LibreDWG's test drawing, via OpenAEC's `ifccad-prototype`, MIT), `$ACADMAINTVER` 20/104/50 | 1: MTEXT `LEADER` (a `304` that is not a marker), style `D8` |
| `gngeovane/Universal_Pen_Holder.dxf` | another program (Solid Edge's fonts), AC1015, `$ACADMAINTVER 6` | 5: MTEXT, background color `0xC8000000` (none); its style's `341 0`/`343 0` and reactors without the multileaders are not AutoCAD's |

ACadSharp's 2000–2018 files have the same 15 (MTEXT, one leader, one line each; proxy graphics in
every version, `92` up to 2007, `160` from 2010).

#### MULTILEADER graphics (phase 3, B38)

Autodesk's 2013 files (AC1027, `$ACADMAINTVER 125`) of the Oxcart project (GPL-3.0), pinned to a
commit. Their numbers have 6 decimals; the proxy graphics are Autodesk's in full.

| File | Multileaders |
|---|---|
| `oxcart/cryo_clamp_upper.dxf` | 2 balloons: block `ISO_BALLOON_CIRCULAR_50` (CIRCLE, ATTDEF `ITEMID` centered) with the value `2` — the attribute after the block (marker 15005), centered on its ink; the leader stops at the circle |
| `oxcart/CY7_diode_clamp.dxf` | 1 balloon: block `*B23` (MTEXT `1`, two ARCs) |
| `oxcart/cryo_interface_v3.dxf` | 1 note `Spalt für \PSchweissverbindung \P…`, right-justified: the spaces before `\P` neither drawn nor counted; underline bottom line (attachment 5) |
| `oxcart/cryo_interface_v2_weld.dxf` | 4: an empty text (its landing a height below the top), notes with underlines |

What the graphics and the extents follow, and where it was seen (the files above, ACadSharp's,
and AutoCAD files found on GitHub without a license, used only locally): the commands and their
order (ACadSharp); the arrowhead only on a segment of at least twice its size (none at 1.82,
drawn at 2.01); an invisible leader (`170 0`) draws the content only; a landing of length 0 has
no marker; the text height is the top of the glyph "A" (SimSun: 179 of 256 where "H" has 175);
a text's extents are its cells (pen to advance, `usWinDescent` to `usWinAscent`: Arial 434/1854,
Arial Unicode MS 555, $EXTMIN/$EXTMAX exact in five drawings), a shape font's its ink; the base
point and the text location by the attachments (139 of the 146 text multileaders exact; the
others: vertical text, a moved leader, an older AutoCAD's text widths).

#### Surfaces and HELIX (phase 3, B39)

| File | Saved by | What it has |
|---|---|---|
| `libre/Surface_2004.dxf` | AutoCAD (LibreDWG's test drawing, via OpenAEC, MIT), AC1018, `$ACADMAINTVER 104` | One each of EXTRUDEDSURFACE `2D8`, LOFTEDSURFACE `34D` (two curves), REVOLVEDSURFACE `366`, SWEPTSURFACE `411` (path class `91 19`), PLANESURFACE `50A` (no subclass of its own) — SAT 20800, the source entities' sizes in bits (`90 472`: 60 bytes), their isolines as proxy graphics; MESH entities whose extents HDxf does not count, so its `$EXTMIN`/`$EXTMAX` is no facit |
| `libre/Helix_2007.dxf` | AutoCAD, AC1021, `$ACADMAINTVER 50` | HELIX `1BF`: 3 turns, 44 control points; its proxy graphics one polyline of 192 points on the spline, `$EXTMIN`/`$EXTMAX` theirs |
| `jscad/3Dlines.dxf` | AutoCAD 2017, AC1027 | Four EXTRUDEDSURFACE (ACIS in ACDSDATA, ASM 223 with 64-bit integers, tag 23), proxy graphics `160 8`; `$EXTMIN`/`$EXTMAX` exact |
| `jscad/3Dhelix.dxf` | AutoCAD 2017, AC1027, `$ACADMAINTVER 125` | Two HELIX (`FB`, `FC`: 3 turns, radius 2.5 and 5, one left-handed), proxy graphics `160 4628` — HELIX in 2013 as in 2010 (phase 5, B65) |
| `jscad/2Dhelix.dxf` | AutoCAD 2017, AC1027, `$ACADMAINTVER 125` | Two HELIX (`D5`, `D6`: 3 turns, radius 2.5, turn height 1/3, one left-handed), proxy graphics `160 4628` |

AutoCAD's 2007 file of the stl-metrics project (`test3D.dxf`, 10 REVOLVEDSURFACE, no license — used
locally) has the surfaces with `92 8` (empty proxy graphics) and the same groups as the 2004 file;
its `$EXTMIN`/`$EXTMAX` is larger than everything in it (not a facit).

#### ezdxf — small files

| File | Version | Format | Entities | Layers | Blocks | Notes |
|---|---|---|---|---|---|---|
| `bin_dxf_r12.dxf` | AC1009 | binary, 1-byte codes | LINE 3 | `0` | 2 | `$DWGCODEPAGE ansi_1252` (lower case) |
| `bin_dxf_r13.dxf` | AC1012 | binary, 2-byte codes | LINE 3 | `0` | 2 | |
| `bin_dxf_r14.dxf` | AC1014 | binary, 2-byte codes | LINE 3 | `0` | 2 | |
| `bin_dxf_r2000.dxf` | AC1015 | binary | TEXT 1 (`ezdxf`) | `0`, `Defpoints` | 2 | |
| `small_r13.dxf` | AC1012 | ASCII | POLYLINE 16 (VERTEX 43) | `0`, `3USMM_PKG`, `FOOT_PRINT` | 2 | `$DWGCODEPAGE dos932`, but no bytes ≥ 0x80 |
| `small_r14.dxf` | AC1014 | ASCII | — | `0` | 2 | no `$DWGCODEPAGE` |
| `dxf_unicode.dxf` | AC1027 | ASCII | — | `0`, `Defpoints`, `Tsch` `C3 BC` `ss mit \U+00FC` | 2 | UTF-8 and `\U+` in one name |
| `Minimal_DXF_AC1009.dxf` | — | ASCII | — | — | — | 35 bytes: an empty ENTITIES section, no HEADER |
| `R12_with_trash_beyond_EOF.dxf` | — | ASCII | CIRCLE 1 | — | — | no HEADER; bytes after `EOF` |
| `r12_blocks_with_no_names.dxf` | AC1009 | ASCII | — | `0`, `Defpoints` | 4 | two BLOCKs without group 2 |
| `Leica_Disto_S910.dxf` | AC1009 | ASCII | LINE 10, POINT 11, TEXT 11 | `LEICA_DISTO_ID`, `LEICA_DISTO_3D`, `LEICA_DISTO_POINT_ENTITY` | 0 | Leica instrument; no layer `0`; first text `1 (IMG_20160506_121212.jpg)` |
| `cc_dxflib.dxf` | AC1009 | ASCII | POLYLINE 1 (VERTEX 5) | `0`, `POLYLINE_001` | 3 | CloudCompare / dxflib |
| `Minimal_DXF_AC1021.dxf` | AC1021 | ASCII | — | `0` | 2 | libdxfrw; no `$DWGCODEPAGE` |

All ezdxf ASCII files use LF line endings.

#### R13 and R14 (phase 7, step 7.1)

No AutoCAD that exists saves R13 or R14 DXF (AutoCAD 2025: R12, 2000 … 2018; the ActiveX `AcSaveAsType` has no
R13/R14 DXF and calls `acR14_dxf` obsolete), so these files cannot be made again: they are what AutoCAD of that
time left in public repositories. **Probable, not proven**: no R13/R14 file says "saved by AutoCAD"; a file counts as
AutoCAD's when its dates fall in the time of that release, its editing time or `$TDUPDATE` shows a session, its
`$MENU`/classes/APPIDs are AutoCAD's or Autodesk products', and nothing points to another program. The reasoning,
the header, tables and entities group by group, and every file below with its SHA256 are in
`doc/reference/hdxf_r13_r14_format.md` (section 1.4).

Pinned in `manifest.cmake`:

| File | Version | Saved by | What it has |
|---|---|---|---|
| `libre/r13_v.dxf` (LibreDWG `test/test-data/r13/v.dxf`) | AC1012, no maintenance, CRLF | AutoCAD R14 or later saving as R13, 1999-11-09 (`$TDCREATE` 1991-01-18, `$TDINDWG` 0.0039, `$MENU Acad`) | The whole R13 header (162 variables), the nine tables, `*MODEL_SPACE`/`*PAPER_SPACE`, 16 LINE and a TEXT, CLASS `DICTIONARYVAR` (`"AutoCAD"`, `90 32768`), root dictionary with `ACAD_GROUP`, `ACAD_MLINESTYLE`, `ACDBVARIABLEDICTIONARY`; family N |
| `gvsig/dxfprueba.dxf` (gvSIG `libGDBMS` test resource, GPL-3.0) | AC1012 | AutoCAD R13, 1995-09-29 / 1998-02-10 (`$TDINDWG` 0.53, `$MENU acad`) | LINE 85, ARC 3, CIRCLE 4, POLYLINE 27 (VERTEX 89), 25 linetypes, no CLASS |
| `spatialauxetic/HEAD.DXF` (ChiangYuChou/SpatialAuxetic, MIT) | AC1014, `$ACADMAINTVER 8`, CRLF | AutoCAD R14.x, a new empty drawing saved 1998-07-14 | Header (164 variables), tables, space blocks; ends after `ENTITIES` (a MATLAB toolbox's template) |
| `spatialauxetic/simp.dxf` | AC1014, 8 | the same header and an OBJECTS section appended by the toolbox | 372 3DFACE |
| `jscad/pyramid-base.dxf` | AC1014, 8 | the same header | 60 POLYLINE polygon meshes (1080 VERTEX): `AcDbPolygonMesh`, `AcDbPolygonMeshVertex` |

`ezdxf/small_r13.dxf` is AutoCAD of the 2000 family saving as R13 (2004-03-04; CLASS application `AutoCAD 2000`,
`LONG_TRANSACTION`, `330` owners: family A2000 in the format profile); `ezdxf/bin_dxf_r13.dxf`, `bin_dxf_r14.dxf`
are **not** AutoCAD (`$TDCREATE` 2020-04-06; ODA's form, binary with two-byte codes) and `small_r14.dxf` is built by a
program (five header variables): observation, not facit.

Used locally and **not pinned** (no distribution right found; copied to `build/test-data/local-r13r14/`, each with
repository, commit, path and SHA256 in `PROVENANCE.txt`; like the other AutoCAD files on GitHub without a licence
above): `az2150` (**AutoCAD R13 for Windows**, `$MENU C:\r13\win\SUPPORT\acad`, 1996), `MII200HB` (Mechanical Desktop on
R13, ELLIPSE 8), `RM2_leg_mechanism`, `ce` (AutoCAD 2002 as R13: hatches left as empty `*X` blocks), seven `Test3_*`
(AutoCAD R14, the 15 default classes), `BL15-1` (AutoCAD 2000 as R14: DIMENSION 12, HATCH 5, MTEXT 142, a TrueType style,
`270 8`), `7H12R10F` (R14: DIMENSION, SPLINE 66), `puppenhaus_4mm` (R14: VIEWPORT with `MVIEW`, a VIEW record),
`Zurn_59251` (AutoCAD 2004 as R14, family A2000), five `altera_*` (R13/R14 with INSERT/ATTRIB, and the image objects
without an IMAGE entity), and observation files whose producer is not AutoCAD or undetermined (`Glock-o-bot`, `bne_JetRanger`,
`FYP_10Marla`, `solderfume`, `BenF18`: the R14 template's `$TDCREATE` and `$TDINDWG` in unrelated drawings; `QDXDivide`).

#### ACIS — drawings of solids (rättning R.9)

Facit for `test/test_acis.cpp`: the `$EXTMIN`/`$EXTMAX` the program that saved each file
wrote. The drawings hold nothing but the solids (and viewports in paper space).

| File | Saved by | ACIS | Solids | `$EXTMIN` / `$EXTMAX` |
|---|---|---|---|---|
| `jscad/3Ddraw.dxf` | AutoCAD 2017 (ASM 222, macOS), 2013 format | SAB `ACIS BinaryFile` 21800 in ACDSDATA | 7: box, wedge, cone (apex at the top), sphere, cylinder, torus, pyramid (a transform with a turn) | (4.950490243203606, 5.999999999999996, -2.5) / (29.00000000000001, 20.0, 9.0) |
| `aspose/conic_pyramid.dxf` | AutoCAD (ASM 219), 2010 format | SAT 21500 in 1/3 | 9: eight whole tori, a cone (its apex the top) | (418.6360698312054, 674.3860089634267, -85.00000000000055) / (1258.636069831206, 1514.386008963427, 1118.593630366642) |
| `ezdxf/3dsolids.dxf` | ODA's ACIS writer, 2010 format | SAT 700 in 1/3 | 4: boxes, a cylinder, a cone and a torus fillet | (0, 0, 0) / (70, 10, 10) |

ACadSharp's drawing has 3DSOLID D65 (a box), D6A (a prism) and REGION D69 in every 2000+
file: SAT 400 (2000), 20800 (2004), 21200 (2007), 21500 (2010), SAB 21800 (2013) and
`ASM BinaryFile4` 22300 (2018). Their boxes are the solids' own data: the box ±2.5 about
its transform's translation (17.77672546982375934, -220.8501226607159253, 2.5), the region
and the prism their vertices moved by (15.27672546982375934, -223.3501226607159253, 0).

## Synthetic files

Written for HDxf, checked in. All have CRLF line endings, like AutoCAD. Each was read by
ezdxf 1.4.5 and passed its audit with no errors or fixes (plan B7). Tests:
`test/test_synthetic_files.cpp`.

| File | Version | What it tests |
|---|---|---|
| `r12_ansi_1252.dxf` | AC1009, `ANSI_1252` | Layer `Lager_` `E5 E4 F6` and TEXT `E5 E4 F6 20 C5 C4 D6 20 80` — åäö ÅÄÖ € as code page bytes |
| `r12_unknown_codepage.dxf` | AC1009, `ANSI_9999` | An unknown code page with bytes ≥ 0x80: TEXT `bytes E5 80 FF` |
| `r2004_unicode_escapes.dxf` | AC1018, `ANSI_1252` | Layer `Lager_\U+00E5\U+00E4\U+00F6`; TEXT `\U+00E5\U+00E4\U+00F6 \U+00C5\U+00C4\U+00D6 \U+20AC`; the same text as ANSI_1252 bytes; `\U+220545,6` |
| `r2004_thumbnail.dxf` | AC1018, `ANSI_1252` | `r2004_unicode_escapes.dxf` with a THUMBNAILIMAGE of our own: `90 64`, a 4×2 24-bit DIB (BITMAPINFOHEADER + 24 bytes: blue row, then red red white white) in two `310` of 32 bytes |
| `r2007_utf8.dxf` | AC1021, `ANSI_1252` | Layer `Lager_åäö` and TEXT `åäö ÅÄÖ €`, `∅45,6` in UTF-8 |
| `r2000_large_handles.dxf` | AC1015 | Every handle from `FFFFFFFFFFFFFF00`; `$HANDSEED FFFFFFFFFFFFFF1C` |
| `r2018_embedded_object.dxf` | AC1032 | `101 Embedded Object` followed by group codes the entity already had, with other values. The values before 101 are the entity's. **MTEXT** at (1, 2), height 2.5, width 50; after 101: 10 = (0, 1), 11 = (99, 99), 40 = 999, 41 = 888. **ATTDEF** (block `EMBEDDED_ATT`, tag `MULTI`) and **ATTRIB** (on an INSERT of it), laid out like AutoCAD 2018's multi-line attributes: own 10 = (5, 6), 40 = 2.5, 1 empty; after 101: 10 = (5, 8.5), 40 = 3.5, 1 = `default line 1\Pdefault line 2` / `attrib line 1\Pattrib line 2` |

The 2000+ files are complete drawings: all nine tables, `*Model_Space`/`*Paper_Space` with
BLOCK_RECORDs, owner handles (330) and a root dictionary with `ACAD_GROUP` in OBJECTS.

## Type register (phase 9)

`typed_types.tsv` and `typed_forms.tsv` are made by `tools/gen_type_registry.py` from the old phase 9 plan's appendices A-C
(`doc/plans/2026-10-25-fas-9-typer.md`) and the reader's dispatch, together with `src/typed_registry_data.inc`
(`python -I tools/gen_type_registry.py --check` says whether they are current; it is a ctest). `typed_types.tsv`: every 0-record
name of the inventory with its kind, status at the inventory, owning step and sample counts per version; `typed_forms.tsv`: the 362 observed
group orders of the untyped types with their sample file, handle and SHA256. They are observations, not facit: only a file AutoCAD
saved is facit. Samples that were in the old inventory's scratch directories (`S/...` in the plan) are not available; the ones in
`build/test-data` are checked against their SHA256 by `test_all_typed_records`.

`tools/inventory_types.py` counts the corpus (this directory's downloads, `doc/facit`, `doc/acad_*_dxf.dxf`) once per content and says
who produced each file; `doc/facit/fas9_register.md` is its report per F9 item. `doc/facit/web/SOURCES.md` lists the files fetched from the web
(AutoCAD origin probable, not proven: they do not count as AutoCAD originals).

### Phase 9 facit and verification

The facit of phase 9 is only what AutoCAD 2025's core saved from our scripts. `doc/facit/fas9_*.scr` are the scripts (plain
AutoCAD commands, one family each); `python -I tools/fas9_facit_run.py doc/facit/fas9_<family>.scr [--input FILE]` runs them
in `accoreconsole` and saves each state as `doc/facit/acad_<version>_dxf_<theme>[_<change>][_bin].dxf` in R12 and 2000 … 2018,
ASCII and binary, with `fas9_facit_log.csv` and the run log; a branch the core has no command for stops the script and is
**ej testat** in the family's step. `doc/facit/fas9_notes/F9.*.md` hold what each F9 item found, and
`doc/facit/fas9_register.md` (made by `tools/fas9_facit_register.py`) says per type which AutoCAD files exist. R13 and R14 have no phase 9 facit:
AutoCAD 2025 cannot save them. `tools/gen_type_registry.py` makes the type register (above), `tools/inventory_types.py` counts the corpus.

`tools/fas9_verify_batch.py` (`--work <directory outside the repository>`, `hdxf_roundtrip` built first) is the check, not a
facit: for each AutoCAD original of `doc/acad_*_dxf.dxf`, `doc/facit` and `build/test-data` it has HDxf read and write the
file unchanged and anew from its typed values (ASCII and binary), and has AutoCAD 2025's core, ODA File Converter and ezdxf read
the original and every output (audit with fixing, extents, object counts, MASSPROP). Files of other producers are robustness
input, marked so; nothing the batch writes becomes facit.

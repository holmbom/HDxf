# R13 and R14 DXF (AC1012, AC1014): the format HDxf writes

Format profile for phase 7 (plan `doc/plans/2026-10-25-fas-7-dxf-r13-r14.md`), step 7.1, written 2026-10-27.
It says what an R13 (AC1012) and an R14 (AC1014) file looks like, group by group, where each statement
comes from, how it differs from what HDxf's 2000 writer (`src/dxf_writer_2000.cpp`) writes today, and what
each later step (7.2 – 7.21) has to do. Nothing here is copied from another library's code.

## 1. Rules, sources and evidence

### 1.1 Rules

* A facit is only a file AutoCAD itself saved. ODA File Converter, ezdxf, LibreDWG and HDxf's own output
  are never facit; no golden files (Erik 2026-10-25, CLAUDE.md "No golden files").
* Where an AutoCAD-saved file shows a form, it decides. Where none does, Autodesk's own DXF reference
  of that release decides. Every deviation between the two is listed in section 13.
* Statements are tagged with their source: `R13:<section>` (Autodesk R13 DXF Reference), `R14:<page>`
  (Autodesk R14 DXF Reference, u14.1.04), `2005:<section>` (AutoCAD 2005 DXF Reference), a file tag
  (section 1.4) with the handle and group, or `HDxf:<function>` for what the code does now.

### 1.2 No AutoCAD that exists can save R13 or R14 DXF

AutoCAD 2025 (and 2027) offers DXF versions R12, 2000, 2004, 2007, 2010, 2013 and 2018 only:

* Autodesk, *AutoCAD ActiveX Reference* 2021, `SaveAs` method: the `AcSaveAsType` constants are
  `acR12_dxf`, `acR14_dwg`, `ac2000_dwg/_dxf`, `ac2004_…`, … `ac2018_dwg/_dxf`; there is no R13 and no R14
  DXF, and the page states `acR14_dxf` is obsolete
  (https://help.autodesk.com/cloudhelp/2021/ENU/AutoCAD-ActiveX-Reference/files/GUID-ED0D0B02-25FC-4ED2-9DEE-2B0C9AA9416A.htm).
* Autodesk's AutoCAD 2025 customization compatibility note lists the DXF versions "R12, 2000, 2004, 2007,
  2010, 2013, 2018" (https://blog.autodesk.io/autocad-2025-interoperability-for-customization/).
* Autodesk's 2016 Customization Guide maps file formats to releases: R14 is the format of R14 only, R12 DXF is
  what R13 and R12 share (https://help.autodesk.com/cloudhelp/2016/ENU/AutoCAD-Customization/files/GUID-D029509F-A2A0-4A7E-BDD5-FE95E7235C5C.htm).

**Consequence.** Erik cannot make an R13 or R14 facit in AutoCAD. The list "Facit som Erik skapar i AutoCAD"
of the plan for phase 7 is empty and has been rewritten (plan, "Facit som Erik skapar i AutoCAD"): what the
search of step 7.1 did not find is a format decision from the references, taken in this file. DWG TrueView
2027's core (`accoreconsole`) reads such files (it does not write them) and is the reader of step 7.24.

### 1.3 References

| Tag | What | Where | Used for |
|---|---|---|---|
| `R13` | Autodesk, *DXF Reference*, "April 95"; updates chapter 13 of the AutoCAD Release 13 Developer's Guide and replaces the group code tables of the Release 13 Customization Guide (publication 00105-010000-5040, 13 September 1994) | https://filemonger.com/specs/dxf/wotsit.org/dxf13/R13DXF/R13DXF.html (HTML mirror of the Word file; the title page reads "DXF Reference 14 / April 95") | every section |
| `R14` | Autodesk, *R14 DXF Reference*, file version **u14.1.04, 24 June 1998** (the same text as appendixes B and C of the AutoCAD Release 14 Customization Guide). Title page and "Revision History" checked. It is **not** `dxf14.pdf` (that name is the 2004 reference) | Wayback copy of http://www.autodesk.com/techpubs/autocad/acadr14/dxf/ (126 pages, `index.htm`, e.g. `https://web.archive.org/web/20010610122609/http://www.autodesk.com:80/techpubs/autocad/acadr14/dxf/hatch_al_u05_c.htm`); page list: `https://web.archive.org/cdx/search/cdx?url=autodesk.com/techpubs/autocad/acadr14/dxf/*` | HEADER (`header_section_al_u05_c`), CLASSES (`classes_section_al_u05_c`), TABLES (`symbol_table*`, `dimstyle`, `layer`, `ltype`, `style`, `view`, `vport`, `ucs`, `appid`, `block95record`), BLOCKS, ENTITIES (all `…_al_u05_c` pages: hatch, boundary path data, lwpolyline, image, mtext, mline, leader, dimension pages, 3dsolid/body/region, insert, attrib, text, polyline, vertex, viewport, acad_proxy_entity), OBJECTS (dictionary, dictionaryvar, group, mlinestyle, imagedef, imagedef_reactor, rastervariables, idbuffer, layer_index, sortentstable, spatial_filter, spatial_index, object_ptr, xrecord), `dxf_revisions` |
| `2005` | AutoCAD 2005 DXF Reference | Autodesk | "Binary DXF File Format": group code is 2 bytes, "1 byte in DXF files prior to AutoCAD Release 14" |
| – | Autodesk, DXF Reference 2015 (DIMSTYLE): 270 "DIMUNIT (obsolete, now use DIMLUNIT AND DIMFRAC)", 287 "DIMFIT (obsolete, now use DIMATFIT and DIMTMOVE)", 276 DIMFRAC, 277 DIMLUNIT, 279 DIMTMOVE, 289 DIMATFIT | https://help.autodesk.com/cloudhelp/2015/ENU/AutoCAD-DXF/files/GUID-F2FAD36F-0CE3-4943-9DAD-A9BCD2AE81DA.htm | section 8 |
| – | Autodesk ObjectARX reference: DIMFIT replaced by DIMATFIT + DIMTMOVE, DIMUNIT split into DIMLUNIT + DIMFRAC in Release 14 (`AcDmUtil::dimfit/dimunit/dimfrac`) | seen in a search summary only: the pages (`help.autodesk.com/view/OARX/2025/ENU/?guid=OARX-RefGuide-AcDmUtil__dimfit_int_int` …) do not load | section 8 (mapping; open question 3) |
| – | Autodesk, *AutoCAD 2000 Migration Guide for Applications*, ch. 8 "File Formats for Solids" p. 126, "ACIS" p. 130 | https://doczz.net/doc/642436/autocad-2000-migration-guide (blocked by a bot challenge on 2026-10-27: not re-read; the plan's reading is used) | section 10 |

The R13 text says `$ACADVER`: "AC1011 in R13" (R13:HEADER); every R13 file says AC1012 and the R14 reference says
"AC1012 = R13, AC1014 = R14" (R14:header_section) — AC1012 is right.

### 1.4 The files

Every file below was read to its end (ASCII as code/value pairs; the binary ones with the width the file uses).
`$TDCREATE`/`$TDUPDATE` are Julian dates of the file, converted here. A file is called **AutoCAD-origin** when
nothing in it points to another program and several things point to AutoCAD: dates inside the time AutoCAD of
that release was sold, `$TDINDWG` > 0 (editing time) or `$TDUPDATE` ≠ `$TDCREATE`, a `$MENU` path of an AutoCAD
installation, the class list AutoCAD of that release registers (R14:classes_section "Default class values"),
class applications of Autodesk products. None of this is a signature: no file in the world says "saved by
AutoCAD" in R13/R14 DXF, so **producer statements are probable, not proven**, and each says on what it rests.

**Pinned in `test/data/manifest.cmake`** (URL pinned to a commit, SHA256; distribution right in the third column):

| Tag | File (local name) | Version / maint. | Producer and evidence | Shows | SHA256 | Right |
|---|---|---|---|---|---|---|
| `v13` | `libre/r13_v.dxf` (LibreDWG `test/test-data/r13/v.dxf`, commit a8ccb1f3…) | AC1012, none; CRLF | AutoCAD-origin, saved 1999-11-09 as R13 by an AutoCAD that knows `DICTIONARYVAR` (R14 or later): `$TDCREATE` 1991-01-18, `$TDUPDATE` 1999-11-09, `$TDINDWG` 0.0038594, `$MENU Acad`, `$HANDLING 1`, the default CLASS of R14 (`DICTIONARYVAR`), `ANSI_1252` | F7.1 in full: the whole R13 header (162 variables incl. `$BLIPMODE`, `$SAVEIMAGES`), nine tables, `*MODEL_SPACE`/`*PAPER_SPACE`, 16 LINE + TEXT, root dictionary with `ACAD_GROUP`, `ACAD_MLINESTYLE`, `ACDBVARIABLEDICTIONARY` (DIMADEC), GROUP dictionary, MLINESTYLE STANDARD | `86531656820742506d5964333c3a82d4554e649174ee67877b3f876f252aa0a1` | GPL-3.0 test data, only read (as the existing LibreDWG files) |
| `dxfp` | `gvsig/dxfprueba.dxf` (iCarto/gvsig-siga, commit 278da442…) | AC1012, none; LF | AutoCAD R13 itself: `$TDCREATE` 1995-09-29, `$TDUPDATE` 1998-02-10, `$TDINDWG` 0.526, `$MENU acad`, `ansi_1252`, no CLASS (so nothing newer than R13 touched it) | 85 LINE, 3 ARC, 4 CIRCLE, 27 POLYLINE/VERTEX/SEQEND, 25 linetypes (complex), R13 native form | `08ba5260458a8e825bffd296869245be25b104c66d2fcc28e5b67e9566f165a6` | GPL-3.0 (gvSIG test resource), only read |
| `head` | `spatialauxetic/HEAD.DXF` (ChiangYuChou/SpatialAuxetic, commit c20201eb…, `MATLAB/HEAD.DXF`) | AC1014, `$ACADMAINTVER 8`; CRLF | AutoCAD R14.x: `$TDCREATE` = `$TDUPDATE` 1998-07-14 (a new drawing saved at once), `$TDINDWG` 0.0024; **an empty new R14 drawing**: `$EXTMIN` 1e20, limits 12×9, `$TEXTSIZE` 0.2, `$MENU .`. The MATLAB toolbox that uses it as a header template ends it after `2 ENTITIES` and has emptied CLASSES | F7.1 for R14: the whole header (164 variables), nine tables, space blocks. No CLASS, no OBJECTS | `a405d99bf176e6f7c49c8f9866413ad2dd757fb6edda63d6e241a055c9aaa25f` | MIT |
| `simp` | `spatialauxetic/simp.dxf` | AC1014, 8; CRLF | `head` plus 372 `3DFACE` and an OBJECTS section the toolbox appended (root dictionary, `ACAD_GROUP`, `ACAD_MLINESTYLE` + STANDARD with fill color 0) | 3DFACE in R14 form | `1ff13a89a47092b3d771c34649b0ca45b2ff822d03e624d8815afe2278b64eb4` | MIT |
| `pyr` | `jscad/pyramid-base.dxf` (jscad/sample-files commit f6f6485e…, `dxf/others/pyramid-base.dxf`) | AC1014, 8; CRLF | the same header as `head` (same `$TDCREATE`, handle seed 5B) with 60 polygon meshes — the toolbox again | POLYLINE as polygon mesh: `100 AcDbPolygonMesh`, `70 16`, `71`, `72`; VERTEX `AcDbPolygonMeshVertex` `70 64` (1080 vertices) | `0e3709cb42438aa23b8534304b46a382622453b5ad2756f6484e4bc392e3e73a` | as the other jscad files |

**Already pinned, now classified** (the plan's four; hashes in the plan and `manifest.cmake`):

| Tag | File | Producer and evidence |
|---|---|---|
| `s13` | `ezdxf/small_r13.dxf` | AutoCAD of the 2000 family saving as R13 on 2004-03-04 (created and saved a minute apart): CLASS application `AutoCAD 2000`, `LONG_TRANSACTION`, `ACDBPLACEHOLDER`, `LAYOUT`, root `TREAT_AS_HARD`, `330` owners everywhere; `dos932` (a Japanese system). ODA never writes `LONG_TRANSACTION`. **Family A2000** (section 1.5) |
| `b13`, `b14` | `ezdxf/bin_dxf_r13.dxf`, `bin_dxf_r14.dxf` | **not AutoCAD**: `$TDCREATE` 2020-04-06 (no AutoCAD of 2020 saves R13/R14 DXF), 18 classes incl. `AcDbMentalRayRenderSettings`, 47 `ACAD_PROXY_OBJECT`. The ODA/Teigha form (binary, two-byte codes). Observation only |
| `s14` | `ezdxf/small_r14.dxf` | not AutoCAD: five header variables, nothing else; built by a program |

**Used locally, not pinned** (no distribution right found; copies in `build/test-data/local-r13r14/` with
`PROVENANCE.txt` giving repository, commit, path and SHA256 — the existing practice of `test/data/README.md`):

| Tag | Local name | Version | Producer and evidence | Shows |
|---|---|---|---|---|
| `az` | `az2150_waterloo.dxf` | AC1012 | **AutoCAD R13 for Windows**: `$MENU C:\r13\win\SUPPORT\acad`, `$TDCREATE` 1996-12-04, `$TDINDWG` 0.0102 | LINE, TEXT, POLYLINE (30), ARC, CIRCLE, POINT; LWPOLYLINE class only; LTYPE DASHED |
| `mii` | `MII200HB_arribada.dxf` | AC1012 | AutoCAD Mechanical Desktop on R13: 145 CLASS records of Autodesk's `AcMe…`, `AcAd…`, `AcDm…` applications, 1997-03-04 | LINE 516, POLYLINE 251, ARC 205, CIRCLE 69, **ELLIPSE 8** |
| `rm2` | `RM2_leg_mechanism.dxf` | AC1012 | AutoCAD-origin, 2000-08-08, CRLF, `$MENU acad`, no CLASS | LINE 7 |
| `ce` | `ce_cadviewer.dxf` | AC1012 | AutoCAD 2000 family saving as R13 (2002-07-10): same class list as `s13` | LINE, ARC, block of 58 LINE; extension dictionaries on the space blocks |
| `t3` | `Test3_*_r14.dxf` (7) | AC1014, 0 | AutoCAD R14 family, 1996–2004; the exact 15-class list of R14:classes_section ("Default class values" + XRECORD); `$MENU C:\WKSPACE\WORSLEY\WAPL\WAPLR13` (Flash_Tank_I), `acad` | LINE, ARC, CIRCLE, POLYLINE, blocks; DIMSTYLE STANDARD with 287 = 0 (FourPlusUnits, ISO8859-1) |
| `z59` | `Zurn_59251_r14_acad2000family.dxf` | AC1014, 9 | AutoCAD 2000 family saving as R14 on 2004-08-14 (created 1995-04-26): classes `AutoCAD 2000`, `330` owners | **DIMENSION 11** with `*D` blocks and MTEXT, DSTYLE override XDATA, LWPOLYLINE, INSERT of `*X` hatch blocks |
| `alt-*` | `altera_*` (5) | AC1012 / AC1014, 0 | AutoCAD-origin, 2003–2008, 16-digit handles (`DF3414A0F832DF94`), `C:\Documents and Settings\…` | INSERT/ATTRIB/ATTDEF/POLYLINE/TEXT/SOLID/POINT; **IMAGEDEF, IMAGEDEF_REACTOR, RASTERVARIABLES, `ACAD_IMAGE_DICT`/`_VARS` in an R13 file with no IMAGE entity** (`alt-piranha`) and in R14 (`alt-yeager-io`, CLASS `IMAGE`/ISM 127) |
| `tmpl` (`glock`, `bne`, `fyp`, `sol`, `bf18`) | `Glock-o-bot_…`, `bne_JetRanger_…`, `FYP_10Marla_…`, `solderfume_…`, `BenF18_…` | AC1014, 0 | **producer undetermined**: `$TDCREATE` 1997-11-20 and `$TDINDWG` 0.0011564931 identical in unrelated drawings (the R14 template's), `$TDUPDATE` 2012–2026 — a program that loads the R14 template and saves. The class list is R14's 24 (15 + `LAYOUT`, `PLOTSETTINGS`, `VBA_PROJECT`, express tools). Observation only | LWPOLYLINE, **HATCH (solid, pattern; polyline/edge boundaries)**, MTEXT, SPLINE, ELLIPSE, ATTDEF, GROUP objects, SORTENTSTABLE |
| `qdx` | `QDXDivide_dxfg18/19_r14.dxf` | AC1014, 0 | `$TDCREATE` = `$TDUPDATE` 2001, `$TDINDWG` 0: a program (CAM) | LINE, ARC; empty CLASSES |
| `bl15` | `BL15-1_davidsmarx_r14_dimension_hatch.dxf` | AC1014, 0 | **AutoCAD 2000 saving as R14**: `$TDCREATE` 2000-07-18, `$TDUPDATE` 2000-07-28, APPIDs `CONTENTTABDATA`, `DRAWINGVARIABLESXDATA`, `CONTENTBLOCKICON` (AutoCAD 2000's DesignCenter), classes `DICTIONARYVAR`, `HATCH` only, no `330` owners, TrueType style with XDATA | **DIMENSION 12** (`*D0…*D11`, `70` 32/160, DSTYLE override `75 1`, `287 4`), **HATCH 5** (solid, associative, polyline and edge loops, seed points, `47 0.33`), MTEXT 142 (in blocks and in `*D` blocks), STYLE `ARIEL` (`3 ARIBLK.TTF`, `1001 ACAD`, `1000 Arial Black`, `1071 34`), DIMSTYLE `STANDARD` with `270 8` and `METRIC` (reactors on it) |
| `ca` | `7H12R10F_schneider_r14_dimension_spline.dxf` | AC1014, 0 | AutoCAD R14 family: `$TDCREATE` 1998-05-20, `$TDUPDATE` 2003-07-19, class list `DICTIONARYVAR` only, no `330`; a Schneider Electric OEM drawing (the repository is MIT, the drawing is not the repository's: not pinned) | DIMENSION 2, SPLINE 66 (`70 11`, `71 3`, 29 knots, 25 control points), MTEXT, INSERT with XDATA |
| `pup` | `puppenhaus_4mm_r14_viewport_view.dxf` | AC1014, 0 | AutoCAD R14 family, `$TDCREATE` 2000-12-01, `$TDUPDATE` 2001-02-26 | **VIEWPORT 2** with `68 1`, `69 1`, `ACAD`/`MVIEW` XDATA (paper space, `67 1`), **a VIEW record** (`OLE1`), LWPOLYLINE 153, MTEXT, HATCH class |

**Not AutoCAD** (found, looked at, not facit): mikedh/trimesh `models/2D/versions/{ae,uc}.r13a/b, r14a/b`
(`$TDUPDATE` 2018-07-28, class list with `AcDbCellStyleMap`, `AcDbSun`, `AcDbMLeaderStyle`: ODA);
LibreDWG `example_r13.dxf`, `example_r14.dxf`, `sample_r14.dxf`, `r14/Leader.dxf` (`$TDUPDATE` 2018-03 … 2019-08,
69 `ACAD_PROXY_OBJECT`: ODA); `as_flat.dxf` of the earlier research (three header variables, tables in the order
VIEW, UCS, APPID, BLOCK_RECORD, LTYPE, VPORT, DIMSTYLE, LAYER, STYLE: an Autodesk Inventor-type exporter, REGION
with SAT 106); every R14 file with `$ACADMAINTVER 9` and `$TDUPDATE` ≥ 2013 (qcad_dxflib, motor-driver,
Random-Projects, keyboard_layout, …). Where `$ACADMAINTVER 9` stands with `AutoCAD 2000` classes and a
`$TDUPDATE` of 2004 (`z59`) it is AutoCAD; with later dates it is ODA's copy of that value.

**Searched and not found** (2026-10-27): `sample_AC1012_{ascii,binary}.dxf`, `sample_AC1014_{ascii,binary}.dxf` do
not exist in ACadSharp: not on `master` and not on any of its 22 branches (they hold only `samples/sample_AC1014.dwg`,
a DWG, and a test JSON), and the commit history of the four names is empty (`commits?path=` returns nothing). GitHub
code search for `AC1012`/`AC1014` with `extension:dxf` (1000 hits per query, partitioned by size and by the class names
`AcDbAlignedDimension`, `AcDbMText`, `AcDbViewport`, `AcDbSpline`, `AcDbHatch`, `AcDbRasterImage`, … — 80 queries) gave several
thousand hits; the header of each was read with a ranged download and the 231 AC1012/AC1014 files with a `$TDCREATE`
before mid-2004 were downloaded and read in full. ixmilia/dxf and netDxf have no R13/R14 DXF. **No AutoCAD-saved binary
R13 or R14 DXF exists in any source found.** AutoCAD-origin R13 files have no DIMENSION, MLINE, TOLERANCE, XLINE, RAY,
LEADER, 3DSOLID/REGION/BODY, HATCH, IMAGE or VIEWPORT with content; R14 files have DIMENSION, HATCH, MTEXT, SPLINE, ELLIPSE,
VIEWPORT, LWPOLYLINE, ATTDEF/ATTRIB and the image objects but no MLINE, TOLERANCE, XLINE, RAY, LEADER, IMAGE entity or
solid; section 14 lists what that leaves open.

### 1.5 Two families of AutoCAD file and the decision between them

R13/R14 files saved by AutoCAD fall into two forms. They differ in the same places every time:

| | **Family N** (native: AutoCAD R13 and R14; R14 saving as R13) | **Family A2000** (AutoCAD 2002 … 2005 saving as R13 or R14) | ODA-type (not AutoCAD) |
|---|---|---|---|
| Files | `v13`, `az`, `dxfp`, `mii`, `rm2`, `head`, `simp`, `t3`, `alt-*`, `bl15`, `ca`, `pup` (AutoCAD R13, R14 and AutoCAD 2000 / 2000i saving as R14: to 2001) | `s13`, `ce`, `z59` (AutoCAD 2002 … 2005: 2002-07, 2004-03, 2004-08) | `b13`, `b14`, trimesh, LibreDWG's |
| `$ACADMAINTVER` | none (R13); 0 or 8 (R14) | none (R13); 9 (R14) | none (R13, no `$BLIPMODE`); 9 (R14) |
| Date of the file | 1991 – 2001 | 2002 – 2005 | 2018 – 2026 |
| `330` owner on records, entities, table headers | **none** (only inside `{ACAD_REACTORS`) | present everywhere (`330 0` on table heads, `330 <table>` on records) | present |
| CLASS application, `90` | `"AutoCAD"`, `ISM`, `"Acad"`, `"<Built In>"`; `90` 0/1/127/32768 | `AutoCAD 2000`; 0 or 32768 | many `ObjectDBX Classes` |
| Root dictionary | `ACAD_GROUP`, `ACAD_MLINESTYLE` (+ `ACDBVARIABLEDICTIONARY` when a DICTIONARYVAR exists) | + `ACAD_PLOTSETTINGS`, `ACAD_LAYOUT`, XDATA `ACAD`/`TREAT_AS_HARD` | + `ACAD_LAYOUT`, `ACAD_PLOTSTYLENAME`, materials, scales |
| Extension dictionary on space blocks | none | `ACAD_LAYOUTSELFREF` | yes |
| Dimension overrides | XDATA `ACAD`/`DSTYLE` as R14:dimension_style_overrides | the same | the same |

**Decision (D1).** A **new** R13/R14 drawing is written in **family N**: it is the form both Autodesk references
describe (no `330` owner in the common groups of R13:"Overall object (or entity) structure" and
R14:common_group_codes_for_graphical_objects; `330` only in the reactor group), it has none of the 2000 objects
the plan forbids (layouts, plot settings, placeholders), and every R13 and R14 file AutoCAD wrote itself up to 2001 (R13, R14, AutoCAD 2000/2000i) has it.
A drawing **read** from either family is written back in its own family: the template mechanism (B12/B96) keeps
the records as read, and **a record written anew follows the family of the file** (owner `330` iff the file's
records have it; class applications as the file's). Family A2000 is therefore a bit of state of the read drawing,
not a second writer: the only differences are the `330` owner groups and what the file already contains.

## 2. File layout

* Sections, in this order (R13:"File Sections", R14:dxf_file_structure): `HEADER`, `CLASSES`, `TABLES`, `BLOCKS`,
  `ENTITIES`, `OBJECTS`, `EOF`. No `THUMBNAILIMAGE`, no `ACDSDATA` (they are 2004 and 2013). Seen in every
  file above. R13 is the first version with CLASSES and OBJECTS (R13:"New for AutoCAD Release13").
* Handles are always present, on every record, including table headers, table records, BLOCK/ENDBLK, VERTEX, SEQEND,
  dictionaries and objects (R13:"New for AutoCAD Release13": "Handles are always enabled. All database objects
  (including symbol tables, symbol table entries, and dictionaries) have handles"). Table records and entities:
  `5` first after `0` ("must be output immediately after 0 group code for nonentity objects": R13:"Overall
  object (or entity) structure"); DIMSTYLE records use `105`. `$HANDSEED` is the next free handle.
  AutoCAD does not use fixed handles: the R14-era new drawings `v13`, `head`, `Glock-o-bot` give the tables the
  handles `BLOCK_RECORD 1, LAYER 2, STYLE 3, LTYPE 5, VIEW 6, UCS 7, VPORT 8, APPID 9, DIMSTYLE A`, the root
  dictionary `C` and the `ACAD_GROUP` dictionary `D`; `az` (R13) and `Test3_FourPlusUnits` do not
  (`az`: VPORT 118, LTYPE 113, LAYER 111, STYLE 112, VIEW 116, UCS 117, APPID 11A, DIMSTYLE 11B, root 11F). No
  reference prescribes a value; **HDxf's own allocation stays** (uniqueness, `$HANDSEED` above the last).
* ASCII: group code right-aligned in 3 columns, 16-bit values right-aligned in 6 (`     1`), 32-bit in 9
  (`    32768`), strings and reals left-aligned, CRLF in `v13`, `head`, `rm2`, `t3`, LF in the files git has
  normalised (`az`, `dxfp`, `mii`): HDxf's existing rule (a new drawing CRLF, a read one its own) applies
  unchanged. Reals: up to 16 significant digits (`326.6616699767926`), `1.000000E+20` for the empty extents.
* Binary: section 11.

## 3. HEADER

Source for the variable list: `v13` (R13, 162 variables) and `head` (R14, 164). The seven AutoCAD-origin R13
files (`v13`, `az`, `dxfp`, `mii`, `rm2`, `ce`, `s13`) have the **same 162 names in the same order**
(checked by script, codes too); the AutoCAD-origin R14 files (`head`, `t3`, `alt-*`, `Glock-o-bot`…) the same 164.

### 3.1 R13 → R14

R14 = R13 with `$SAVEIMAGES` removed (the last variable of R13), `$ACADMAINTVER` (70) directly after `$ACADVER`,
and at the end `$PROXYGRAPHICS` (70) after `$CMLSCALE`, then `$MEASUREMENT` (70, last). `$BLIPMODE` (after
`$LIMCHECK`) is in both: ODA's files leave it out (`b13`, `b14`), AutoCAD's have it (R14:header_section lists it).
Seen: `head` positions 1–3 `$ACADVER`, `$ACADMAINTVER`, `$DWGCODEPAGE`; last two `$PROXYGRAPHICS 1`, `$MEASUREMENT 0`.
`$SAVEIMAGES` is in neither reference; R13 writes it (`v13`: 1).

### 3.2 Order, groups and values

Value types follow the group code (as in HDxf's codec and the references): `1`, `2`, `3`, `6`, `7`, `8` strings; `5` handle (hex string); `10/20/30` (and `11…`) reals
of a point; `40`, `50` reals; `70` 16-bit integer; in binary with the same widths as elsewhere (section 11). A variable with
several groups lists them in the order of the file (`10 20 30`). The table is the R14 header in order (`head`). "R13 pos" is the place in `v13` (R13 lacks 2, 163, 164 and has `$SAVEIMAGES`
last, position 162, 70). "Constant" says whether the value is the same in every AutoCAD-origin file of that
version (R13: 7 files, R14: 10): such a value is AutoCAD's own default. The last column is the value in AutoCAD 2025's own
R12 new drawing, `doc/acad_r12_dxf.dxf` (metric; HDxf's `default_r12_headers` follows it with a few exceptions).

| # R14 | Variable | Group(s) | R13 pos | New R14 drawing (HEAD.DXF) | Constant in AutoCAD-origin files | HDxf R12 default (AutoCAD 2025) |
|---:|---|---|---:|---|---|---|
| 1 | `$ACADVER` | 1 | 1 | `AC1014` | not a default | `AC1009` |
| 2 | `$ACADMAINTVER` | 70 | - | `8` | not a default |  |
| 3 | `$DWGCODEPAGE` | 3 | 2 | `ANSI_1252` | not a default |  |
| 4 | `$INSBASE` | 10,20,30 | 3 | `0.0 0.0 0.0` | varies | `0.0 0.0 0.0` |
| 5 | `$EXTMIN` | 10,20,30 | 4 | `1.000000E+20 1.000000E+20 1.000000E+20` | not a default | `0.0 0.0 0.0` |
| 6 | `$EXTMAX` | 10,20,30 | 5 | `-1.000000E+20 -1.000000E+20 -1.000000E+20` | not a default | `100.0 70.0 0.0` |
| 7 | `$LIMMIN` | 10,20 | 6 | `0.0 0.0` | R13+R14 | `0.0 0.0` |
| 8 | `$LIMMAX` | 10,20 | 7 | `12.0 9.0` | varies | `420.0 297.0` |
| 9 | `$ORTHOMODE` | 70 | 8 | `0` | varies | `0` |
| 10 | `$REGENMODE` | 70 | 9 | `1` | R13+R14 | `1` |
| 11 | `$FILLMODE` | 70 | 10 | `1` | R13+R14 | `1` |
| 12 | `$QTEXTMODE` | 70 | 11 | `0` | R13+R14 | `0` |
| 13 | `$MIRRTEXT` | 70 | 12 | `1` | R13+R14 | `0` |
| 14 | `$DRAGMODE` | 70 | 13 | `2` | R13+R14 | `2` |
| 15 | `$LTSCALE` | 40 | 14 | `1.0` | varies | `1.0` |
| 16 | `$OSMODE` | 70 | 15 | `0` | R14 | `37` |
| 17 | `$ATTMODE` | 70 | 16 | `1` | R13+R14 | `1` |
| 18 | `$TEXTSIZE` | 40 | 17 | `0.2` | varies | `2.5` |
| 19 | `$TRACEWID` | 40 | 18 | `0.05` | varies | `1.0` |
| 20 | `$TEXTSTYLE` | 7 | 19 | `STANDARD` | varies | `STANDARD` |
| 21 | `$CLAYER` | 8 | 20 | `0` | varies | `0` |
| 22 | `$CELTYPE` | 6 | 21 | `BYLAYER` | R13 | `BYLAYER` |
| 23 | `$CECOLOR` | 62 | 22 | `256` | R13 | `256` |
| 24 | `$CELTSCALE` | 40 | 23 | `1.0` | R13+R14 |  |
| 25 | `$DELOBJ` | 70 | 24 | `1` | R13+R14 |  |
| 26 | `$DISPSILH` | 70 | 25 | `0` | R13+R14 |  |
| 27 | `$DIMSCALE` | 40 | 26 | `1.0` | R13+R14 | `1.0` |
| 28 | `$DIMASZ` | 40 | 27 | `0.18` | varies | `2.5` |
| 29 | `$DIMEXO` | 40 | 28 | `0.0625` | varies | `0.625` |
| 30 | `$DIMDLI` | 40 | 29 | `0.38` | varies | `3.75` |
| 31 | `$DIMRND` | 40 | 30 | `0.0` | R13+R14 | `0.0` |
| 32 | `$DIMDLE` | 40 | 31 | `0.0` | R13 | `0.0` |
| 33 | `$DIMEXE` | 40 | 32 | `0.18` | varies | `1.25` |
| 34 | `$DIMTP` | 40 | 33 | `0.0` | R13+R14 | `0.0` |
| 35 | `$DIMTM` | 40 | 34 | `0.0` | R13+R14 | `0.0` |
| 36 | `$DIMTXT` | 40 | 35 | `0.18` | varies | `2.5` |
| 37 | `$DIMCEN` | 40 | 36 | `0.09` | varies | `2.5` |
| 38 | `$DIMTSZ` | 40 | 37 | `0.0` | R13+R14 | `0.0` |
| 39 | `$DIMTOL` | 70 | 38 | `0` | R13+R14 | `0` |
| 40 | `$DIMLIM` | 70 | 39 | `0` | R13+R14 | `0` |
| 41 | `$DIMTIH` | 70 | 40 | `1` | varies | `0` |
| 42 | `$DIMTOH` | 70 | 41 | `1` | varies | `0` |
| 43 | `$DIMSE1` | 70 | 42 | `0` | R13+R14 | `0` |
| 44 | `$DIMSE2` | 70 | 43 | `0` | R13+R14 | `0` |
| 45 | `$DIMTAD` | 70 | 44 | `0` | varies | `1` |
| 46 | `$DIMZIN` | 70 | 45 | `0` | varies | `8` |
| 47 | `$DIMBLK` | 1 | 46 | (empty) | R13+R14 | (empty) |
| 48 | `$DIMASO` | 70 | 47 | `1` | R14 | `1` |
| 49 | `$DIMSHO` | 70 | 48 | `1` | varies | `1` |
| 50 | `$DIMPOST` | 1 | 49 | (empty) | R13+R14 | (empty) |
| 51 | `$DIMAPOST` | 1 | 50 | (empty) | R13+R14 | (empty) |
| 52 | `$DIMALT` | 70 | 51 | `0` | R13+R14 | `0` |
| 53 | `$DIMALTD` | 70 | 52 | `2` | varies | `3` |
| 54 | `$DIMALTF` | 40 | 53 | `25.4` | varies | `0.03937007874016` |
| 55 | `$DIMLFAC` | 40 | 54 | `1.0` | R13+R14 | `1.0` |
| 56 | `$DIMTOFL` | 70 | 55 | `0` | varies | `1` |
| 57 | `$DIMTVP` | 40 | 56 | `0.0` | R13 | `0.0` |
| 58 | `$DIMTIX` | 70 | 57 | `0` | R13+R14 | `0` |
| 59 | `$DIMSOXD` | 70 | 58 | `0` | R13+R14 | `0` |
| 60 | `$DIMSAH` | 70 | 59 | `0` | R13+R14 | `0` |
| 61 | `$DIMBLK1` | 1 | 60 | (empty) | R13+R14 | (empty) |
| 62 | `$DIMBLK2` | 1 | 61 | (empty) | R13+R14 | (empty) |
| 63 | `$DIMSTYLE` | 2 | 62 | `STANDARD` | varies | `ISO-25` |
| 64 | `$DIMCLRD` | 70 | 63 | `0` | varies | `0` |
| 65 | `$DIMCLRE` | 70 | 64 | `0` | varies | `0` |
| 66 | `$DIMCLRT` | 70 | 65 | `0` | varies | `0` |
| 67 | `$DIMTFAC` | 40 | 66 | `1.0` | R13 | `1.0` |
| 68 | `$DIMGAP` | 40 | 67 | `0.09` | varies | `0.625` |
| 69 | `$DIMJUST` | 70 | 68 | `0` | R13+R14 |  |
| 70 | `$DIMSD1` | 70 | 69 | `0` | R13+R14 |  |
| 71 | `$DIMSD2` | 70 | 70 | `0` | R13+R14 |  |
| 72 | `$DIMTOLJ` | 70 | 71 | `1` | R14 |  |
| 73 | `$DIMTZIN` | 70 | 72 | `0` | R14 |  |
| 74 | `$DIMALTZ` | 70 | 73 | `0` | R13+R14 |  |
| 75 | `$DIMALTTZ` | 70 | 74 | `0` | R13+R14 |  |
| 76 | `$DIMFIT` | 70 | 75 | `3` | R14 |  |
| 77 | `$DIMUPT` | 70 | 76 | `0` | R13+R14 |  |
| 78 | `$DIMUNIT` | 70 | 77 | `2` | R13 |  |
| 79 | `$DIMDEC` | 70 | 78 | `4` | varies |  |
| 80 | `$DIMTDEC` | 70 | 79 | `4` | varies |  |
| 81 | `$DIMALTU` | 70 | 80 | `2` | R13 |  |
| 82 | `$DIMALTTD` | 70 | 81 | `2` | varies |  |
| 83 | `$DIMTXSTY` | 7 | 82 | `STANDARD` | varies |  |
| 84 | `$DIMAUNIT` | 70 | 83 | `0` | R13+R14 |  |
| 85 | `$LUNITS` | 70 | 84 | `2` | R13+R14 | `2` |
| 86 | `$LUPREC` | 70 | 85 | `4` | R13 | `4` |
| 87 | `$SKETCHINC` | 40 | 86 | `0.1` | varies | `1.0` |
| 88 | `$FILLETRAD` | 40 | 87 | `0.5` | varies | `0.0` |
| 89 | `$AUNITS` | 70 | 88 | `0` | R13+R14 | `0` |
| 90 | `$AUPREC` | 70 | 89 | `0` | varies | `0` |
| 91 | `$MENU` | 1 | 90 | `.` | varies | `.` |
| 92 | `$ELEVATION` | 40 | 91 | `0.0` | R13+R14 | `0.0` |
| 93 | `$PELEVATION` | 40 | 92 | `0.0` | R13+R14 | `0.0` |
| 94 | `$THICKNESS` | 40 | 93 | `0.0` | R13+R14 | `0.0` |
| 95 | `$LIMCHECK` | 70 | 94 | `0` | R13+R14 | `0` |
| 96 | `$BLIPMODE` | 70 | 95 | `0` | R14 | `0` |
| 97 | `$CHAMFERA` | 40 | 96 | `0.5` | varies | `0.0` |
| 98 | `$CHAMFERB` | 40 | 97 | `0.5` | varies | `0.0` |
| 99 | `$CHAMFERC` | 40 | 98 | `1.0` | varies |  |
| 100 | `$CHAMFERD` | 40 | 99 | `0.0` | R13+R14 |  |
| 101 | `$SKPOLY` | 70 | 100 | `1` | R13 | `0` |
| 102 | `$TDCREATE` | 40 | 101 | `2451008.519973958` | not a default | `2461339.523298610933125` |
| 103 | `$TDUPDATE` | 40 | 102 | `2451008.523538426` | not a default | `2461339.5242592599242926` |
| 104 | `$TDINDWG` | 40 | 103 | `0.0024055556` | not a default | `0.0009606481` |
| 105 | `$TDUSRTIMER` | 40 | 104 | `0.0024055556` | not a default | `0.0009606481` |
| 106 | `$USRTIMER` | 70 | 105 | `1` | R13+R14 | `1` |
| 107 | `$ANGBASE` | 50 | 106 | `0.0` | R13+R14 | `0.0` |
| 108 | `$ANGDIR` | 70 | 107 | `0` | R13+R14 | `0` |
| 109 | `$PDMODE` | 70 | 108 | `0` | R13+R14 | `0` |
| 110 | `$PDSIZE` | 40 | 109 | `0.0` | R13+R14 | `0.0` |
| 111 | `$PLINEWID` | 40 | 110 | `0.0` | R14 | `0.0` |
| 112 | `$COORDS` | 70 | 111 | `1` | R14 | `1` |
| 113 | `$SPLFRAME` | 70 | 112 | `0` | R13+R14 | `0` |
| 114 | `$SPLINETYPE` | 70 | 113 | `6` | R13+R14 | `6` |
| 115 | `$SPLINESEGS` | 70 | 114 | `8` | R13+R14 | `8` |
| 116 | `$ATTDIA` | 70 | 115 | `0` | R13+R14 | `1` |
| 117 | `$ATTREQ` | 70 | 116 | `1` | R13 | `1` |
| 118 | `$HANDLING` | 70 | 117 | `1` | R14 | `1` |
| 119 | `$HANDSEED` | 5 | 118 | `5B` | not a default | `319` |
| 120 | `$SURFTAB1` | 70 | 119 | `6` | R13 | `6` |
| 121 | `$SURFTAB2` | 70 | 120 | `6` | R13 | `6` |
| 122 | `$SURFTYPE` | 70 | 121 | `6` | R13+R14 | `6` |
| 123 | `$SURFU` | 70 | 122 | `6` | R13 | `6` |
| 124 | `$SURFV` | 70 | 123 | `6` | R13 | `6` |
| 125 | `$UCSNAME` | 2 | 124 | (empty) | R13+R14 | (empty) |
| 126 | `$UCSORG` | 10,20,30 | 125 | `0.0 0.0 0.0` | R13+R14 | `0.0 0.0 0.0` |
| 127 | `$UCSXDIR` | 10,20,30 | 126 | `1.0 0.0 0.0` | R13 | `1.0 0.0 0.0` |
| 128 | `$UCSYDIR` | 10,20,30 | 127 | `0.0 1.0 0.0` | R13+R14 | `0.0 1.0 0.0` |
| 129 | `$PUCSNAME` | 2 | 128 | (empty) | R13+R14 | (empty) |
| 130 | `$PUCSORG` | 10,20,30 | 129 | `0.0 0.0 0.0` | R13+R14 | `0.0 0.0 0.0` |
| 131 | `$PUCSXDIR` | 10,20,30 | 130 | `1.0 0.0 0.0` | R13+R14 | `1.0 0.0 0.0` |
| 132 | `$PUCSYDIR` | 10,20,30 | 131 | `0.0 1.0 0.0` | R13+R14 | `0.0 1.0 0.0` |
| 133 | `$USERI1` | 70 | 132 | `0` | R13+R14 | `0` |
| 134 | `$USERI2` | 70 | 133 | `0` | R13+R14 | `0` |
| 135 | `$USERI3` | 70 | 134 | `0` | R13+R14 | `0` |
| 136 | `$USERI4` | 70 | 135 | `0` | R13+R14 | `0` |
| 137 | `$USERI5` | 70 | 136 | `0` | R13+R14 | `0` |
| 138 | `$USERR1` | 40 | 137 | `0.0` | R13+R14 | `0.0` |
| 139 | `$USERR2` | 40 | 138 | `0.0` | R13+R14 | `0.0` |
| 140 | `$USERR3` | 40 | 139 | `0.0` | R13+R14 | `0.0` |
| 141 | `$USERR4` | 40 | 140 | `0.0` | R13+R14 | `0.0` |
| 142 | `$USERR5` | 40 | 141 | `0.0` | R13+R14 | `0.0` |
| 143 | `$WORLDVIEW` | 70 | 142 | `1` | R13+R14 | `1` |
| 144 | `$SHADEDGE` | 70 | 143 | `3` | R13+R14 | `3` |
| 145 | `$SHADEDIF` | 70 | 144 | `70` | R13+R14 | `70` |
| 146 | `$TILEMODE` | 70 | 145 | `1` | R13+R14 | `1` |
| 147 | `$MAXACTVP` | 70 | 146 | `48` | varies | `64` |
| 148 | `$PINSBASE` | 10,20,30 | 147 | `0.0 0.0 0.0` | R13+R14 |  |
| 149 | `$PLIMCHECK` | 70 | 148 | `0` | R13+R14 | `0` |
| 150 | `$PEXTMIN` | 10,20,30 | 149 | `1.000000E+20 1.000000E+20 1.000000E+20` | R14 | `0.0 0.0 0.0` |
| 151 | `$PEXTMAX` | 10,20,30 | 150 | `-1.000000E+20 -1.000000E+20 -1.000000E+20` | R14 | `0.0 0.0 0.0` |
| 152 | `$PLIMMIN` | 10,20 | 151 | `0.0 0.0` | R13+R14 | `0.0 0.0` |
| 153 | `$PLIMMAX` | 10,20 | 152 | `12.0 9.0` | varies | `12.0 9.0` |
| 154 | `$UNITMODE` | 70 | 153 | `0` | R13+R14 | `0` |
| 155 | `$VISRETAIN` | 70 | 154 | `1` | varies | `1` |
| 156 | `$PLINEGEN` | 70 | 155 | `0` | R13+R14 | `0` |
| 157 | `$PSLTSCALE` | 70 | 156 | `1` | varies | `1` |
| 158 | `$TREEDEPTH` | 70 | 157 | `3020` | R13+R14 |  |
| 159 | `$PICKSTYLE` | 70 | 158 | `1` | R13+R14 |  |
| 160 | `$CMLSTYLE` | 2 | 159 | `STANDARD` | R13+R14 |  |
| 161 | `$CMLJUST` | 70 | 160 | `0` | R13+R14 |  |
| 162 | `$CMLSCALE` | 40 | 161 | `1.0` | varies |  |
| 163 | `$PROXYGRAPHICS` | 70 | - | `1` | varies |  |
| 164 | `$MEASUREMENT` | 70 | - | `0` | varies |  |

### 3.3 Values of a new drawing (D2)

A new R13/R14 drawing's header is, variable by variable:

1. **Constant in AutoCAD's own R13/R14 files** (column "Constant"): that value. These agree with AutoCAD 2025's R12
   new drawing except five, where the R13/R14 files win because they are what AutoCAD of that release wrote:
   `$MIRRTEXT 1` (2025's R12: 0), `$ATTDIA 0` (1), `$OSMODE 0` (R14; 37), `$PEXTMIN 1.000000E+20 ×3` and
   `$PEXTMAX -1.000000E+20 ×3` (R14; 0 0 0) — the empty-extents values of `head`, `rm2`, `t3`.
   Constants AutoCAD 2025's R12 file does not have: `$CELTSCALE 1.0`, `$DELOBJ 1`, `$DISPSILH 0`, `$DIMJUST 0`,
   `$DIMSD1 0`, `$DIMSD2 0`, `$DIMTOLJ 1`, `$DIMTZIN 0`, `$DIMALTZ 0`, `$DIMALTTZ 0`, `$DIMFIT 3`, `$DIMUPT 0`,
   `$DIMUNIT 2`, `$DIMALTU 2`, `$DIMAUNIT 0`, `$CHAMFERD 0.0`, `$PINSBASE 0 0 0`, `$TREEDEPTH 3020`,
   `$PICKSTYLE 1`, `$CMLSTYLE STANDARD`, `$CMLJUST 0`.
2. **Varies between files** (limits, text and dimension sizes, units precision, current layer, …: they depend on
   the template): the value HDxf's R12 and 2000 new drawing has (AutoCAD 2025's template: metric, `$LIMMAX 420 297`,
   `$TEXTSIZE 2.5`, `$DIMASZ 2.5` …), so a drawing differs between versions in format only. The imperial values of
   AutoCAD's own empty R14 drawing (`head`) are in the table for anyone who wants that template.
3. **Not defaults** (the plan: view, date, timers are not defaults): `$TDCREATE`, `$TDUPDATE`, `$TDINDWG`,
   `$TDUSRTIMER` (HDxf's house value 0.0 as in its R12/2000 header; AutoCAD writes real Julian dates, and no reader
   needs them), `$HANDSEED` (next free handle), `$EXTMIN/$EXTMAX/$PEXTMIN/$PEXTMAX` (from the geometry; empty:
   `1.000000E+20 ×3` / `-1.000000E+20 ×3` as AutoCAD writes it in all of `head`, `v13` paper space, `rm2`),
   `$INSBASE`/`$PINSBASE` 0, `$DWGCODEPAGE` (`ANSI_1252`; R13 files from AutoCAD have `ansi_1252` or `ANSI_1252`,
   `dos932`, `ISO8859-1`: the reader decodes any case), `$MENU` (`.`: `head`, `Glock-o-bot`; R13 files: `acad`).
4. **Names are upper case** in R13/R14: `$TEXTSTYLE STANDARD`, `$DIMSTYLE STANDARD`, `$DIMTXSTY STANDARD`,
   `$CMLSTYLE STANDARD`, `$CELTYPE BYLAYER`, `$CLAYER 0` (every symbol name in all 27 AutoCAD-origin files read
   is upper case — script check of LAYER, LTYPE, STYLE, APPID, DIMSTYLE, UCS, VIEW, VPORT, BLOCK_RECORD and BLOCK names).
5. R13 only: `$SAVEIMAGES 1` (`v13`, `az`: 1; `dxfp`: 0, `rm2`: 0 — a user setting; default 1); `$BLIPMODE 0`
   (R14 constant 0; R13 files 0 or 1).
6. R14 only: `$PROXYGRAPHICS 1` (every R14 file, R14:header_section "Controls the saving of proxy object images"),
   `$MEASUREMENT` 0 = English, 1 = metric (R14:header_section); HDxf's metric template gives 1 (`head` is imperial: 0).

### 3.4 `$ACADMAINTVER` (D3)

R13: **absent** (all 7 AutoCAD-origin files; R13 predates it). R14: present as `70`, directly after `$ACADVER`;
the R14 reference says "Maintenance version number (should be ignored)" (R14:header_section). Values seen:
**0** in AutoCAD R14 (`t3`, `alt-*`, the template family), **8** in `head`/`simp`/`pyr` (the AutoCAD R14 that saved
`head`), **9** in family A2000 (`z59`) and in ODA's files. A new R14 drawing gets **0**: the value of the AutoCAD that
wrote the family-N form, the form the plan's D1 chooses. (The plan's B88 says 9 "enligt huvudprovet"; the main sample
`bin_dxf_r14` is ODA's; the choice has no meaning for any reader.) A read file keeps its value (B12). Never 20
(2000), never group 90 (2018): `maintenance_as_written()` writes 70 below 2018 — correct for R14.

### 3.5 Deviations between references and files (header)

| Id | Reference says | Files say | Decision |
|---|---|---|---|
| H1 | R13:HEADER `$ACADVER`: "AC1011 in R13" | AC1012 in every R13 file; R14:header_section "AC1012 = R13, AC1014 = R14" | AC1012 |
| H2 | R13:HEADER `$DIMBLK 2` (arrow block name) | `$DIMBLK 1` in all R13 and R14 files; R14:header_section says 1 | code 1 (HDxf:write_header_r2000 already forces 1) |
| H3 | R13:HEADER `$DIMTXSTY 70` | `7` (`v13`: `7 STANDARD`); R14:header_section 7 | code 7 |
| H4 | neither reference lists `$SAVEIMAGES` | every R13 file has it, last, code 70 | write it in R13 |
| H5 | R13:HEADER "$TILEMODE 1 for previous release compatibility mode" | 1 in all | 1 |
| H6 | R13:HEADER `$DIMALTTZ` text on zeros | R14:header_section has the 0–3 feet/inches text; values 0 | 0 |

### 3.6 What the 2000 header code must stop doing

`HDxf:write_header_r2000` (`dxf_writer_2000.cpp`, 456–535): (a) takes `default_r2000_headers()` for 2000: R13/R14 need
their own lists (the 162/164 variables above, in this order); (b) **removes** every variable that is in the R12
header and not in 2000's (`$HANDLING`, `$BLIPMODE`, … the `r12_only` set) — R13/R14 have exactly those; (c) appends any
variable the drawing has and the list lacks (`vars.push_back(hv)`): for R13/R14 a 2000-only variable of the
drawing (`$CELWEIGHT`, `$INSUNITS`, …) must be a version error (B98), not a header line; (d) writes `$LASTSAVEDBY`,
`$REQUIREDVERSIONS`, `$XCLIPFRAME` (2004+) — none in R13/R14; (e) `$DIMBLK` code 1 and `maintenance_as_written`
(70) are right. `$HANDLING` is `1` (R14:header_section describes it as "Next available handle", a copy of `$HANDSEED`'s text; the value is 1
in all files but one that has 10241).

## 4. CLASSES

`CLASS` records: `0 CLASS`, `1` DXF name, `2` C++ class name, `3` application, `90` proxy flags, `280`, `281`
(R13:CLASSES, R14:classes_section). **No `91`** (instance count: 2004). R14:classes_section adds the `32768 = R13
format proxy` bit to `90`.

**R14 default classes** (R14:classes_section "Default class values"; the 15 records of `t3` = those 14 + `XRECORD`, in the order of
`t3`). AutoCAD wrote either all 15 (`t3`, `alt-*`) or just the classes the drawing uses (`ca`: `DICTIONARYVAR`; `bl15`: `DICTIONARYVAR`,
`HATCH`; `pup`: `DICTIONARYVAR`, `LWPOLYLINE`, `HATCH`; `az`: `LWPOLYLINE`):

| `1` | `2` | `3` | `90` | `280` | `281` |
|---|---|---|---:|---:|---:|
| `LWPOLYLINE` | `AcDbPolyline` | `"AutoCAD"` | 0 | 0 | 1 |
| `IMAGE` | `AcDbRasterImage` | `ISM` | 127 | 0 | 1 |
| `OLE2FRAME` | `AcDbOle2Frame` | `""` | 0 | 0 | 1 |
| `HATCH` | `AcDbHatch` | `"ACAD_SEDONA"` | 0 | 0 | 1 |
| `DICTIONARYVAR` | `AcDbDictionaryVar` | `"AutoCAD"` | 0 | 0 | 0 |
| `RASTERVARIABLES` | `AcDbRasterVariables` | `ISM` | 0 | 0 | 0 |
| `IMAGEDEF` | `AcDbRasterImageDef` | `ISM` | 0 | 0 | 0 |
| `IMAGEDEF_REACTOR` | `AcDbRasterImageDefReactor` | `ISM` | 1 | 0 | 0 |
| `IDBUFFER` | `AcDbIdBuffer` | `"Acad"` | 0 | 0 | 0 |
| `SPATIAL_FILTER` | `AcDbSpatialFilter` | `"Acad"` | 0 | 0 | 0 |
| `XRECORD` | `AcDbXrecord` | `"<Built In>"` | 0 | 0 | 0 |
| `SORTENTSTABLE` | `AcDbSortentsTable` | `"AutoCAD"` | 0 | 0 | 0 |
| `LAYER_INDEX` | `AcDbLayerIndex` | `"Acad"` | 0 | 0 | 0 |
| `SPATIAL_INDEX` | `AcDbSpatialIndex` | `"Acad"` | 0 | 0 | 0 |
| `OBJECT_PTR` | `CAseDLPNTableRecord` | `"ASE-LPNTableRecord"` | 1 | 0 | 0 |

(`t3`/`Glock-o-bot`: `3 "AutoCAD"` is written **with the quotes** as part of the string, ISM without; the
R14 reference prints the application column without quotes. `tmpl` files add `LAYOUT`, `PLOTSETTINGS`,
`ACDBDICTIONARYWDFLT`, `ACDBPLACEHOLDER` (`3 "AutoCAD 2000"`, `90 1`), `VBA_PROJECT`, `WIPEOUT`/`WIPEOUTVARIABLES`,
`RTEXT`, `ARCALIGNEDTEXT`: classes of products, kept when read.)

**R13** classes: `v13` (R14 saved as R13) has the one record `DICTIONARYVAR … "AutoCAD" 32768 0 0`; `az` the one record
`LWPOLYLINE … "AutoCAD" 32768 0 1`; `dxfp`, `rm2`, `head`: none. Family A2000: `s13`/`ce`: `ACDBDICTIONARYWDFLT`,
`ACDBPLACEHOLDER`, `LAYOUT`, `LONG_TRANSACTION`, `LWPOLYLINE`, `HATCH`, `XRECORD`, `3 AutoCAD 2000`, `90 32768`
(`ce` writes the last four twice). In R13 files from AutoCAD **`90` is 32768** (the R13 proxy bit) on every record
written by R14-or-later; the R14 files have 0/1/127.

**Rule (D4).** CLASSES holds one record per class **used by the objects written** and, for a read file, **all the
records it had** (B92; the registry is the file's, in its order). A new R13 drawing has none; a new R14 drawing none
until it has an object whose class is above (the 15 are what AutoCAD writes; `head` has none because it is a template
for a tool). The values are those of the table; R13 writes `90` as 32768 for the records it writes. `HDxf:classes_used`
(2000) lists 2000+ classes (`ISM`, `ObjectDBX Classes`, with `90` 4095 …): the R13/R14 list is **separate** with
this table's values; `write_class` drops the `91` below 2004 already.

## 5. TABLES

### 5.1 Table heads

`0 TABLE`, `2 <name>`, `5 <handle>`, `100 AcDbSymbolTable`, `70 <count>` (R13:TABLES; R14:symbol_tables_in_dxf_files;
`v13`, `head`). **No `330 0`** (2000's `write_table_head` writes it) and no `AcDbDimStyleTable`/`71`/`340` on DIMSTYLE
(2000's `write_table_r2000`). Table names upper case. Order: `VPORT, LTYPE, LAYER, STYLE, VIEW, UCS, APPID, DIMSTYLE,
BLOCK_RECORD` in every file (R13:TABLES: "The order of the tables may change, but the LTYPE table always precedes the
LAYER table"; `bin_dxf_r13` too). Empty tables (VIEW, UCS) are written. `ENDTAB` closes each.

`70` is "the maximum number of entries … do not use the count in the 70 group as an index" (R13:TABLES; R14:symbol_table_group_codes
"Maximum number of entries in table"). What AutoCAD wrote (script over 18 files, `70` / records): LAYER, STYLE, VIEW, UCS,
APPID, DIMSTYLE: the number of records (more if something was deleted: `v13` LAYER 6/5, DIMSTYLE 2/1); **LTYPE: records − 2**
in 11 of 18 files (BYBLOCK and BYLAYER are not counted: `v13` 1/3, `az` 2/4, `dxfp` 25/27, `mii` 3/5, `rm2` 1/3, `s13` 1/3, `ce` 1/3, `head` 1/3,
`z59` 3/5, `alt-le` 2/4, `t3` VII 2/4; the other seven `t3` files and `Glock-o-bot` have 3/3 or 4/4 — probably an older high-water
mark); **BLOCK_RECORD: records − 2** in 16 of 18 (`*MODEL_SPACE` and `*PAPER_SPACE` not counted: 0/2 in 11 files, 1/3 in two, 4/6 `ce`,
80/82 `alt-le`, 19/21 `z59`; the exceptions had a deleted block: `v13` 1/2, `t3` VII 3/2); VPORT: 2/1 in the R13 files `az`, `dxfp`,
`rm2`, `ce`, `s13` (3/1 `mii`, 5/1 `v13`), 1/1 in the R14 files `t3` (six of seven), `Glock-o-bot`, `alt-le` (3/1 `head`, 5/1 `z59`). **Decision (D5):** a new drawing writes the
number of records, except LTYPE and BLOCK_RECORD (records − 2) and VPORT (1 in R14, 2 in R13); a read table keeps the file's
count and raises it for each added record (HDxf's B12 rule, `table 70 stays the file's`). The count is not read as an index by any
reader that follows the references.

### 5.2 Record forms

Every record: `0 <type>`, `5 <handle>` (`105` for DIMSTYLE), [`102 {ACAD_XDICTIONARY` `360` `102 }`], [`102 {ACAD_REACTORS`
`330` … `102 }`], `100 AcDbSymbolTableRecord`, `100 <subclass>`, `2 <name>`, `70 <flags>` (R13:TABLES, R14:common_group_codes_for_symbol_table_entries).
**No `330` owner** in family N (the reactor group's `330` only). Flag bits in `70`: 16 xref-dependent, 32 resolved, 64 referenced
(AutoCAD sets 64 on records an entity used: `mii` all 64, `dxfp` linetypes 64; "need not be set by programs that write
DXF files": R13:TABLES). `0` is right for a new record.

| Table | Subclass | Groups after `2`, `70` (in AutoCAD's order) | Source |
|---|---|---|---|
| VPORT `*ACTIVE` | `AcDbViewportTableRecord` | `10 20`, `11 21`, `12 22`, `13 23`, `14 24`, `15 25`, `16 26 36`, `17 27 37`, `40`, `41`, `42`, `43`, `44`, `50`, `51`, `71`, `72`, `73`, `74`, `75`, `76`, `77`, `78` (`68`, `69` are never saved) | `v13` 43, `az` 1F1, R13:VPORT, R14:vport |
| LTYPE | `AcDbLinetypeTableRecord` | `3` description, `72` (65), `73` n, `40` total, then per element `49` + `74` (0 for a plain element); complex element: `74` 1 absolute rotation / 2 text / 4 shape, `75`, `340` style handle, `46`, `50`, `44`, `45`, `9` text | `az` DASHED 81, `dxfp`, `mii` CENTER; R14:ltype |
| LAYER | `AcDbLayerTableRecord` | `62` colour (negative: off), `6` linetype. 70: 1 frozen, 2 frozen in new viewports, 4 locked | `v13` E, R13:LAYER, R14:layer |
| STYLE | `AcDbTextStyleTableRecord` | `40` fixed height, `41` width factor, `50` oblique, `71` generation flags, `42` last height, `3` font file, `4` big font (`""`); 70 bit 1 shape file, bit 4 vertical | `v13` F, R13:STYLE |
| VIEW | `AcDbViewTableRecord` | `40`, `10 20`, `41`, `11 21 31`, `12 22 32`, `42`, `43`, `44`, `50`, `71` | R13:VIEW, R14:view (70 bit 1 paper space view); `pup` `OLE1` (R14): `70 0`, `40 1254.95611`, `10 350.303296 20 253.785923`, `41 2217.742751`, `11 0 21 0 31 1`, `12 0 22 0 32 0`, `42 50`, `43 0`, `44 0`, `50 0`, `71 0`. No R13 file has a VIEW record |
| UCS | `AcDbUCSTableRecord` | `10 20 30`, `11 21 31`, `12 22 32` | R13:UCS, R14:ucs. No AutoCAD-origin file has one |
| APPID | `AcDbRegAppTableRecord` | (nothing more; 70 bit 1: XDATA not written by SAVEASR12) | `v13` 10, R14:appid |
| DIMSTYLE | `AcDbDimStyleTableRecord` | section 8 | `v13` 36, R13:DIMSTYLE |
| BLOCK_RECORD | `AcDbBlockTableRecord` | nothing more (no `340`, no `102 {BLKREFS`, no `70/280/281`) | `v13` 17/14, R13:BLOCK_RECORD, R14:block95record |

The standard records of a new drawing: LTYPE `BYBLOCK`, `BYLAYER` (`3` empty), `CONTINUOUS` (`3 Solid line`), all `72 65`, `73 0`, `40 0.0`;
LAYER `0` (`70 0`, `62 7`, `6 CONTINUOUS`); STYLE `STANDARD` (`40 0.0`, `41 1.0`, `50 0.0`, `71 0`, `42 0.2`, `3 txt`, `4` empty: `head`,
`az` has `SIMPLEX`, `v13` `ROMAND`: font per template); APPID `ACAD`; VPORT `*ACTIVE`; DIMSTYLE `STANDARD`; BLOCK_RECORD `*MODEL_SPACE`,
`*PAPER_SPACE` (that order; `v13`, `az`, `head`).

### 5.3 Differences from the 2000 writer (what 7.6 changes)

| 2000 writer | R13/R14 | Source |
|---|---|---|
| `330 0` on table heads; `330 <table>` on records | none | `v13`, `az`, `head` |
| names `Standard`, `Continuous`, `ByBlock`, `ByLayer`, `*Active`, `*Model_Space`, `*Paper_Space`, `Standard` dimstyle | `STANDARD`, `CONTINUOUS`, `BYBLOCK`, `BYLAYER`, `*ACTIVE`, `*MODEL_SPACE`, `*PAPER_SPACE` | all files |
| VPORT: `281`, `65`, `110 111 112`, `79`, `146`, `345/346` (`write_ucs_refs`), 2007+ `332/333/348/60/61/292/282/141/142/63/421/361` | none | `v13`, `az`; R14:vport |
| LAYER: `290` (plot), `370` lineweight, `390` plot style handle, 2007+ `347`, 2013 `348`, transparency XDATA | none: `70`, `62`, `6` only | `v13`, `az`; R13:LAYER |
| STYLE: XDATA `ACAD` font family | **written for a TrueType style in R14** as the 2000 writer does: `3` the font file (`ARIBLK.TTF`), `4` empty, then `1001 ACAD`, `1000 <family>`, `1071 <flags>` (`bl15` ARIEL, AutoCAD 2000 as R14; `z59`); R13: no sample (open question 8) | `bl15`, `z59` |
| VIEW: `281`, `72`, `110–112`, `79`, `146`, 2007+ | `40 10 20 41 11 12 42 43 44 50 71` | R13:VIEW |
| UCS: `79`, `146`, `346` | `10…12` only | R13:UCS |
| DIMSTYLE: `AcDbDimStyleTable` `71`/`340` head; only groups that differ from the default; `341–344`, `345–347`, `371/372`, XDATA; `340` style handle | all groups, 5/6/7 names, 105, 270, 287 | section 8 |
| BLOCK_RECORD: `340` layout handle, preview, `102 {BLKREFS`, 2007+ `70/280/281`, XDICTIONARY | none | `v13`, R14:block95record |
| LTYPE: `74` bit 8 from 2013 | bits 1, 2, 4 | R14:ltype |
| 2000 `has_standard`/default records created for names that differ in case | one set, upper case | – |

## 6. BLOCKS, BLOCK_RECORD and the space blocks

* BLOCKS holds, in this order, `*MODEL_SPACE`, `*PAPER_SPACE`, then the user's blocks (`v13`, `az`, `head`, `ce`; R13:BLOCKS:
  "there is always two extra, empty definitions … *MODEL_SPACE and *PAPER_SPACE"). Names **upper case with underscore**: `*MODEL_SPACE`,
  `*PAPER_SPACE` (2000: `*Model_Space`, `*Paper_Space`); `3` repeats the name; `1` (xref path) is written empty on every BLOCK
  (`1=` in all files).
* `BLOCK`: `0 BLOCK`, `5`, [XDATA/reactors after the handle: R13:BLOCKS "The block table handle along with any xdata and persistent
  reactors will appear in each block definition immediately following the BLOCK record"], `100 AcDbEntity`, [`67 1` for the paper space block],
  `8` layer, `100 AcDbBlockBegin`, `2` name, `70` flags, `10 20 30`, `3` name, `1` xref path. `ENDBLK`: `5`, `100 AcDbEntity`, [`67 1`], `8`,
  `100 AcDbBlockEnd`. **No `330`** (family N). The BLOCK's layer is `0`. 70 bit 1 anonymous, 2 has ATTDEF, 4 xref, 16 xref-dependent, 32
  resolved, 64 referenced (R13:BLOCK; R14:block adds 8 "xref overlay").
* The 2000 writer differences (`write_block_begin_r2000`): `330 <record>`, `4` description (only when non-empty), group
  order of the head (`write_entity_head`: `5`, `102`, `330`, `100`): R13/R14 have none of the `330`s and no `4`.
* Block names in a drawing built here: upper case (all names in all 27 files are). Anonymous: `*D<n>` dimension blocks,
  `*X<n>` hatch blocks (R13), `*U<n>` (`ce`: `*X0`…`*X3` with `70 1`).
* **R13 hatch blocks.** R13's representation of a hatch is an anonymous block `*X<n>` with flag 1 referenced by an INSERT with
  XDATA `ACAD` ("hatch block properties", R13:INSERT) and boundary entities with XDATA `ACAD`/`ASC_BOUNDS`. AutoCAD 2002's R13 export of
  hatched drawings (`ce`) keeps the `*X` blocks **empty**, writes no HATCH entity and leaves `ASC_BOUNDS` XDATA on the boundary ARC/LINE.
  See section 7.4.

### 6.1 Paper space: where the entities go (D6)

* The ENTITIES section holds model and paper space entities; `67 1` marks paper space (R13:Common Entity Group Codes; R14:common
  group codes). R13:BLOCKS: "Model Space and Paper Space Entity Segregation: The interleaving between model space and paper space
  will no longer occur … all paper space entities will be output, followed by model space entities." R14 has no such
  sentence (R14:blocks_in_dxf_files). **AutoCAD's own R14 file with both, `puppenhaus_4mm` (`pup`, found in 7.16), has the two VIEWPORTs (`67 1`) after
  all of its model space entities, as the last records of ENTITIES** (as every R12 and 2000+ file); the other files with both are not AutoCAD-origin
  (`Random-Projects`, `pinball`: the VIEWPORT, `67 1`, **after** the model entities). No R13 file has paper space entities.
* The `*PAPER_SPACE` BLOCK/ENDBLK carry `67 1` and the paper-space entities appear in ENTITIES, **not** inside the block
  (R13:"The entities contained in these definitions will still appear in the ENTITIES section for compatibility"). Model-space entities
  are likewise only in ENTITIES.
* **Decision (D6, revised in the wave 2 integration):** **R13** writes **paper space entities first, then model space**, as the R13 reference says
  (no R13 file exists; no AutoCAD-origin file contradicts it): a stable partition of `dwg.entities()` (`write_entities_r2000`). **R14** writes the
  entities **in the drawing's list order** with `67` from `props.paper_space`, as AutoCAD's own R14 file `puppenhaus_4mm` has it (VIEWPORT last;
  the R14 reference has no sentence on the order), so a program that adds the viewport last gets AutoCAD's file. A read file keeps its order (B12).
  **Open question 4** is settled for R14 by that file; for R13 it stays the reference's sentence until 7.24's read in the core.
* No LAYOUT, no `410`, no `AcDbPlotSettings`; paper space is the one `*PAPER_SPACE` block (R13:BLOCKS, R14:VIEWPORT). The extra
  header variables `$PSLTSCALE`, `$PEXTMIN/MAX`, `$PLIMMIN/MAX`, `$PUCS*`, `$PELEVATION`, `$PINSBASE`, `$PLIMCHECK`, `$TILEMODE`, `$MAXACTVP`
  describe it. The root dictionary of a new drawing has no `ACAD_LAYOUT`.

### 6.2 VIEWPORT (the old form)

R13:VIEWPORT = R14:viewport (identical): `100 AcDbViewport`, `10 20 30` centre (paper space), `40` width, `41` height, `68` status
(**R13: 1 = on but fully off screen; R14: -1**; 0 off; positive = stack order, 1 the active viewport), `69` id (always 1 for the paper
space viewport; "never saved" otherwise), then XDATA `ACAD` `MVIEW` in this fixed order: `1000 MVIEW`, `1002 {`, `1070 16`, `1010 1020 1030`
target, `1010 1020 1030` direction, `1040` twist, `1040` height, `1040 1040` centre, `1040` lens, `1040` front, `1040` back, `1070` view mode,
`1070` circle zoom, `1070` fast zoom, `1070` UCSICON, `1070` snap, `1070` grid, `1070` snap style, `1070` snap isopair, `1040` snap angle,
`1040 1040` snap base, `1040 1040` snap spacing, `1040 1040` grid spacing, `1070` hidden-in-plot, `1002 {`, `1003` frozen layers…,
`1002 }`, `1002 }`. "The order of the extended group codes is important." This is exactly what HDxf stores for R12
(`view_from_mview`); **the R13/R14 viewport is written as R12's (plus `100 AcDbEntity` `AcDbViewport` and `67 1`) and not as the 2000
VIEWPORT** (2000: `12 13 14 15 16 17 42 43 44 45 50 51 72 90 1 340 …`). AutoCAD's own R14 has it (`pup`, two viewports:
`67 1`, `8 0`, `100 AcDbViewport`, `10 4.506328 20 3.4 30 0.0`, `40 15.021094`, `41 8.5`, `68 1`, `69 1`, then the XDATA with 1070 16 …
`1040 50.0` lens, `1070 0 1070 100 1070 1 1070 1 1070 0 1070 0 1070 0 1070 0`, `1040 0.0 ×3`, `1040 10.0 ×4`, `1070 0`, `1002 {`, `1002 }`, `1002 }`);
R13 only in the reference (open question 5).


## 7. ENTITIES

### 7.1 The common head

`0 <type>`, `5 <handle>`, [`102 {ACAD_REACTORS` `330 <handle>`… `102 }`], [`102 {ACAD_XDICTIONARY` `360 <dictionary>` `102 }`],
`100 AcDbEntity`, [`67 1`], `8 <layer>`, [`6 <linetype>`], [`62 <colour>`], [`48 <scale>`], [`60 1`], then the type's subclasses,
then XDATA (R13:Common Entity Group Codes and "Overall object (or entity) structure", R14:common_group_codes_for_graphical_objects;
files `v13`, `az`, `mii`, `Glock-o-bot`: `5 100 8 6 62`, `48` inside the AcDbEntity part: `mii` LINE `8=L2 48=0.3`).
The order of the two 102 groups: R13 and R14 show the reactor group first and the extension dictionary second; no AutoCAD-origin R13/R14
entity has an extension dictionary (HDxf's 2000 writer puts the dictionary first, as AutoCAD 2000+ files do): follow the reference.

**Absent in R13/R14** (the 2000 head, `write_entity_head`, writes them): `330` owner (family N), `370` lineweight, `380`/`390` plot style,
`347` material, `440` transparency, `284` shadow mode, `92`/`160` proxy graphics size. A 2000-only value in the drawing is a version
error (B93, B98), not a dropped group.

### 7.2 The R12 types

| Type | Subclass chain and groups (family N) | Source |
|---|---|---|
| LINE | `AcDbLine`: [39], `10 20 30`, `11 21 31`, [210] | `v13` 24; R13:LINE |
| POINT | `AcDbPoint`: [39], `10 20 30`, [210], [50] | `az` 8D; R13:POINT |
| CIRCLE | `AcDbCircle`: [39], `10 20 30`, `40`, [210] | `az` 4A |
| ARC | `AcDbCircle`: `10 20 30`, `40`, then `AcDbArc`: `50 51` (thickness and 210 per R13:ARC) | `az` 42 |
| TRACE, SOLID | `AcDbTrace`: `10 11 12 13` (each `x y z`), [39], [210] | `alt-cuda` EABC2557…; R13:SOLID |
| 3DFACE | `AcDbFace`: `10 11 12 13`, [70] invisible-edge flags | `simp` B2 |
| SHAPE | `AcDbShape`: [39], `10`, `40`, `2` name, [50 41 51 210] | R13:SHAPE, R14:shape |
| TEXT | `AcDbText`: [39], `10`, `40`, `1`, [50 41 51 7 71 72 11], [210]; then **`100 AcDbText` again**, then [`73`] | `v13` 2C (`72 1 11 … 100 AcDbText`), `az` 7, R13:TEXT, R14:text |
| ATTDEF | text part as TEXT without the second marker (`7`, `71`, `72`, `11` inside it), then `AcDbAttributeDefinition`: `3` prompt, `2` tag, `70`, [73], [74] | `bne` 1EE, R13:ATTDEF |
| ATTRIB | text part, then `AcDbAttribute`: `2` tag, `70`, [73], [74] | `alt-arm-lab` DF3414A0FD4F844D |
| INSERT | `AcDbBlockReference`: [`66 1`], `2`, `10 20 30`, [41 42 43 50 70 71 44 45], [210]; ATTRIBs, `SEQEND` | `alt-arm-lab` …44C |
| POLYLINE | `AcDb2dPolyline` / `AcDb3dPolyline` / `AcDbPolygonMesh` / `AcDbPolyFaceMesh`: `66 1`, `10 20 30` (elevation in 30), [39], `70`, [40 41 71 72 73 74 75], [210] | `az` E, `pyr` B1 (mesh); the references list only the first two (D13.7) |
| VERTEX | `AcDbVertex`, then `AcDb2dVertex` / `AcDb3dPolylineVertex` / `AcDbPolygonMeshVertex` / `AcDbPolyFaceMeshVertex`: `10 20 30`, [40 41 42], `70`, [50], [71–74] | `az` 124, `pyr` B2 (`70 64`) |
| SEQEND | `100 AcDbEntity`, `8` (no own subclass); after POLYLINE, and after INSERT with `66 1` | `az` 132, `alt` …44E |
| DIMENSION | section 7.3 | `z59` 8EB, R13/R14 dimension pages |
| VIEWPORT | section 6.2 | R13/R14:viewport |

`SEQEND`, `VERTEX`, `ATTRIB` have their own handle, layer (`8`; the owner's if none was set) and `100 AcDbEntity`; **no `330` to the polyline or
insert** in family N. A paper space polyline or insert's children carry `67 1` too (R13:Common Entity Group Codes).

### 7.3 DIMENSION

Common: `100 AcDbDimension`, `2` block name (`*D<n>`), `10 20 30` definition point, `11 21 31` text midpoint, `70` type, [`1` text], [`53`], [`51`], [210], `3` style name
(R13:DIMENSION, R14:common_dimension_group_codes; `z59` 8EB: `2=*D15 10 11 70=160 1=E 3=ROMANS`). `70`: 0 rotated/horizontal/vertical, 1
aligned, 2 angular, 3 diameter, 4 radius, 5 angular 3-point, 6 ordinate (+64 X-type), **+32 "always set in R13 and later releases"**
(R14:common_dimension_group_codes; the 2000 writer sets 32 only for blocks it built), +128 text at a user position.
AutoCAD-origin R14 records (`bl15` 91: `100 AcDbEntity`, `8 DIMS`, `100 AcDbDimension`, `2 *D0`, `10 20 30`, `11 21 31`, `70 160`, `3 METRIC`,
`100 AcDbAlignedDimension`, `13`, `14`, `50 90.0`, `100 AcDbRotatedDimension`; `ca` 6D3 `70 32`; `bl15` 153 with `12 120.0 22 0.0 32 0.0` for a baseline clone; `1 %%c<>.00/h8\PINPUT` text override).
**Absent**: `71` text attachment, `42` measurement (the 2000 writer writes both), `72`/`41`/`73–75`, `280` version.
Subclasses: aligned/linear/rotated `AcDbAlignedDimension`: [12 22 32], `13 23 33`, `14 24 34`, [`50`], [`52`], then `AcDbRotatedDimension` for a
rotated one; radius `AcDbRadialDimension`, diameter `AcDbDiametricDimension`: `15 25 35`, `40`; angular `AcDb3PointAngularDimension`: `13 14 15 16`
(R13/R14 give this one marker for angular dimensions, type 2 and 5; the 2000 writer writes `AcDb2LineAngularDimension` for type 2: open
question 6); ordinate `AcDbOrdinateDimension`: `13 14`. Dimension overrides: XDATA `1001 ACAD`, `1000 DSTYLE`, `1002 {`, pairs `1070 <code>` +
value, `1002 }` (R14:dimension_style_overrides; `z59`; `1070 340` + `1005 <handle>` for a text style override). The `*D<n>` block holds the
drawn dimension; HDxf builds it (`dimension_block.cpp`): the geometry follows B91's model; arrow block names: open question 7.

### 7.4 The R13 family and R14

| Type | R13 | R14 | Source / what differs |
|---|---|---|---|
| ELLIPSE | `AcDbEllipse`: `10 20 30`, `11 21 31`, [210], `40`, `41`, `42` | same | `mii` 154, `bne` 1DA; R13:ELLIPSE |
| XLINE, RAY | `AcDbXline`/`AcDbRay`: `10`, `11` | same | R13:XLINE/RAY; no AutoCAD-origin sample |
| MTEXT | `AcDbMText`: `10`, `40`, `41`, `71`, `72` (**1 LR, 2 RL, 3 TB, 4 BT**), `1` (chunks of **255**), `3`, `7`, [210], `11` | `72`: **1, 3, 5 (by style)** (R14 revision u14.1.04), chunks of **250**; adds `42` characters' width, `43` height, `50` rotation | R13:MTEXT, R14:mtext, `dxf_revisions`; `bne` 1EA, `z59` 139F (XDATA `ACAD`/`MTEXTBEGIN … MTEXTEND` with 73, 44, 74 in family A2000) |
| TOLERANCE | `AcDbFcf`: `3` style, `10`, `1`, [210], `11` | same | R13:TOLERANCE; override XDATA like DIMENSION |
| LEADER | `AcDbLeader`: `3`, `71`, `72`, `73`, `74`, `75`, `40`, `41`, `76`, `10…`, `77`, `340` annotation, `210`, `211`, `212` "direction of annotation offset" | `212` is the block-reference insertion point offset, **`213` the annotation placement offset** (new) | R13:LEADER, R14:leader; override XDATA |
| SPLINE | `AcDbSpline`: [210], `70`, `71`, `72`, `73`, `74`, `42`, `43`, [44], [12], [13], `40…`, `10…`, [`41…`], `11…` | same | `bne` 1E0; R13:SPLINE |
| MLINE | `AcDbMline`: `2` style, `340`, `40`, `70`, `71` (**1 open, 3 closed**), `72`, `73`, `10`, [210], per vertex `11 12 13` and per element `74`, `41…`, `75`, `42…` | `71`: **bit 1 unlocked, 2 closed, 4 suppress start caps, 8 suppress end caps** (R14 revision) | R13:MLINE, R14:mline, `dxf_revisions`; no AutoCAD-origin sample |
| 3DSOLID, REGION, BODY | `AcDbModelerGeometry`: `70 1`, `1…`, `3…` | same | R13/R14 pages; section 10 |
| OLEFRAME / OLE2FRAME | `AcDbOleFrame` | `AcDbOle2Frame` (CLASS `OLE2FRAME`) | R13:OLEFRAME, R14:ole2frame; kept as read (phase 9) |
| LWPOLYLINE | **not in R13**: error | `AcDbPolyline`: `90` n, `70` (1 closed, 128 plinegen), [43], [38], [39], per vertex `10 20`, [40 41 42], [210] | R14:lwpolyline; `bne`, `Glock`: same chain |
| HATCH | **not in R13**: error | `AcDbHatch`: `10 20 30` (elevation), [210…], `2` name, `70` solid, `71` associative, `91` n loops, per loop `92` flags + polyline (`72` has-bulge, `73` closed, `93` n, `10 20 [42]`…) or edges (`93`, per edge `72` 1–4 and its groups), `97` n sources + `330…`; `75`, `76`, [52 41 77 78 + pattern lines], `47`, `98` n seeds, `10 20`… | R14:hatch, boundary_path_data, pattern_data; **`bl15` 53, 62, 67, 6E5, 76C (AutoCAD 2000 as R14)**: `2 SOLID 70 1 71 1 91 2 92 7 72 1 73 1 93 2 10 20 42 … 97 1 330 4C … 75 0 76 1 47 0.33 98 2 10 20 10 20`, loops flagged 7 (external+polyline+derived), a polyline loop with `72 0 73 1 93 4`, source objects `330` listed per loop; `62` colour in the head |
| IMAGE (+objects) | **not in R13**: error | `AcDbRasterImage`: `90` (class version 0), `10`, `11` U, `12` V, `13` size, `340` IMAGEDEF, `70`, `280`, `281`, `282`, `283`, `360` reactor, `71`, `91`, `14…` | R14:image; objects section 9 |
| ACAD_PROXY_ENTITY | – | `AcDbProxyEntity`: `90` 498, `91`, `92`, `310…`, `93`, `310…`, `330/340/350/360…`, `94` 0 | R14:acad_proxy_entity; kept as read (phase 9) |

### 7.5 Decisions: LWPOLYLINE, HATCH, IMAGE in R13 (D7–D9)

* **D7 LWPOLYLINE.** R13: `"LWPOLYLINE requires DXF R14 or later"`. Sources: R13's entity list has no LWPOLYLINE (R13:ENTITIES, the
  list of 27 entity types); R14 introduces it with a class (R14:lwpolyline, "Default class values"). AutoCAD's own R13 export confirms the
  entity does not exist there: `s13` registers `LWPOLYLINE`/`AcDbPolyline` and has 16 POLYLINE/43 VERTEX/16 SEQEND (the polylines of the
  source drawing exported as the old form), `az` registers it with no entity.
* **D8 HATCH.** R13: `"HATCH requires DXF R14 or later"`. Sources: no HATCH in the R13 entity list or class list; R13 knows hatches only as an
  anonymous block `*X<n>` (BLOCK flag 1, R13:BLOCK "generated by hatching") inserted by an INSERT with XDATA `ACAD` "information related to
  hatch block properties" (R13:INSERT) — a form whose XDATA the reference does not describe; AutoCAD 2002's own R13 export of drawings with hatches (`ce`)
  writes **no** HATCH and **no** INSERT of the blocks: the `*X0…*X3` blocks stay as **empty** anonymous blocks with `70 1`, and the boundary ARC/LINE
  carry `1001 ACAD`, `1000 ASC_BOUNDS`, `1002 {`, `1070 19`, `1005 <handle>`, `1002 }`. R14: native form above, class `HATCH`/`AcDbHatch`/`"ACAD_SEDONA"`/`0 0 1`.
* **D9 IMAGE, IMAGEDEF, IMAGEDEF_REACTOR, RASTERVARIABLES.** R13: `"IMAGE requires DXF R14 or later"` (IMAGEDEF, IMAGEDEF_REACTOR and RASTERVARIABLES:
  the same message with their own name). Sources: none of the four is in the R13 reference. AutoCAD's own R13-marked file `alt-piranha` shows
  what an R13 export does to images: it keeps the CLASS records `IMAGE`, `RASTERVARIABLES`, `IMAGEDEF`, `IMAGEDEF_REACTOR` (ISM, `127 0 1` …), the
  dictionary entries `ACAD_IMAGE_DICT`/`ACAD_IMAGE_VARS` and 18 IMAGEDEF_REACTORs with `330 0` — but **no IMAGE entity**: the entity cannot exist in R13
  (the objects are what is left). R14: native; `alt-yeager-io` (R14) shows the objects, the entity's form is the reference's (open question 11: no
  AutoCAD-origin IMAGE entity record in any source).

### 7.6 Fields of the 2000 writer that R13/R14 do not have

`330` owners; `370/380/390`; `440`; `347`; `284`; DIMENSION `71`, `42`, `73–75`, `280`; TEXT/ATTRIB `280` (2010 version, 2007 lock), `71`/`72` multiline attribute
groups; MTEXT `73`, `44`, `90`, `63`, `45`, `441`, `46`, `75`, `101`, `1000 ACAD_MTEXT_*`; LWPOLYLINE `91` (2010); POLYLINE/VERTEX `91`; HATCH `450–470`, `63`,
`421`, `453…` (2004 gradient/colour), `98` seed groups the same, `47` pixel size only for pattern fills; IMAGE `290` (2010 clip inside), `360` is R14's;
INSERT/BLOCK extension dictionaries `ACAD_LAYOUTSELFREF`; TEXT-style XDATA; LAYER/VPORT/VIEW/UCS fields of section 5.3. Each belongs to B93/B98:
a non-default value is a version error naming the version where it first exists, a default is simply not written.

## 8. DIMSTYLE

R13:DIMSTYLE, R14:dimstyle; `v13` 36, `az` 121, `head` 1D, `z59` BB6/BCC. Record head: `0 DIMSTYLE`, **`105` handle**, [reactors `330…` as `z59` BCC: the
dimensions that use the style], `100 AcDbSymbolTableRecord`, `100 AcDbDimStyleTableRecord`, `2` name, `70` flags. Then **every group, always, in this
order** (every file read that has a DIMSTYLE; AutoCAD's STANDARD, imperial template):

`3 DIMPOST`, `4 DIMAPOST`, `5 DIMBLK`, `6 DIMBLK1`, `7 DIMBLK2` (**names**, empty strings written as `5=` …), `40 DIMSCALE` 1.0, `41 DIMASZ` 0.18,
`42 DIMEXO` 0.0625, `43 DIMDLI` 0.38, `44 DIMEXE` 0.18, `45 DIMRND` 0, `46 DIMDLE` 0, `47 DIMTP` 0, `48 DIMTM` 0, `140 DIMTXT` 0.18, `141 DIMCEN` 0.09,
`142 DIMTSZ` 0, `143 DIMALTF` 25.4, `144 DIMLFAC` 1, `145 DIMTVP` 0, `146 DIMTFAC` 1, `147 DIMGAP` 0.09, `71 DIMTOL` 0, `72 DIMLIM` 0, `73 DIMTIH` 1,
`74 DIMTOH` 1, `75 DIMSE1` 0, `76 DIMSE2` 0, `77 DIMTAD` 0, `78 DIMZIN` 0, `170 DIMALT` 0, `171 DIMALTD` 2, `172 DIMTOFL` 0, `173 DIMSAH` 0, `174 DIMTIX` 0,
`175 DIMSOXD` 0, `176 DIMCLRD` 0, `177 DIMCLRE` 0, `178 DIMCLRT` 0, **`270 DIMUNIT` 2**, `271 DIMDEC` 4, `272 DIMTDEC` 4, `273 DIMALTU` 2, `274 DIMALTTD` 2,
**`340` handle of the STYLE record** (instead of `DIMTXSTY`), `275 DIMAUNIT` 0, `280 DIMJUST` 0, `281 DIMSD1` 0, `282 DIMSD2` 0, `283 DIMTOLJ` 1, `284 DIMTZIN` 0,
`285 DIMALTZ` 0, `286 DIMALTTZ` 0, **`287 DIMFIT` 3**, `288 DIMUPT` 0.

(R14:dimstyle prints 176 as "DIMDLRD": a misprint of DIMCLRD; R13:DIMSTYLE has it right. 2000 files: `DIMCLRD` is `176`.)

**Differences from the 2000 writer (`write_dimstyle_r2000`)**: the 2000 writer writes only the groups that differ from `autocad_dim_style()`; **R13/R14 write all of them** (every
`3`…`7` and `40`…`288` line is in all twelve files that have a DIMSTYLE). The 2000 codes that do not exist in R13/R14 and must not be written: `49`, `50`, `69`, the second `70`, `79`, `90`,
`148`, `179`, `276 DIMFRAC`, `277 DIMLUNIT`, `278`, `279 DIMTMOVE`, `289 DIMATFIT`, `290`, `295`, `341–347`, `371`, `372`, the `AcDbDimStyleTable` head and the XDATA `ACAD_DSTYLE_*`.
The arrow blocks are **names** (5, 6, 7), not handles (342–344); `DIMLDRBLK` has no code (R13/R14 have none: leaders use `DIMBLK`); `340` is always written and is the STYLE's handle
(`v13` `340=F` = STANDARD's `5=F`).

**Mapping (D10), legacy value ↔ HDxf's modern fields.** Autodesk: 270 "DIMUNIT (obsolete, now use DIMLUNIT and DIMFRAC)", 287 "DIMFIT (obsolete, now use DIMATFIT and DIMTMOVE)"
(DXF Reference 2015, DIMSTYLE). R14:header_section: DIMUNIT 1 Scientific, 2 Decimal, 3 Engineering, 4 Architectural (stacked), 5 Fractional (stacked), 6 Architectural, 7 Fractional
(R13 lists 1–4). **A file of AutoCAD 2000 as R14 has `270 8` and `273 8` (DIMUNIT, DIMALTU)** in `bl15` (STANDARD, `171 4`, `274 4`): 8 = "Windows desktop", the legacy form of DIMLUNIT 6
(R14:header_section does not list 8: D13.14). DIMLUNIT: 1 Scientific, 2 Decimal, 3 Engineering, 4 Architectural, 5 Fractional, 6 Windows desktop; DIMFRAC: 0 horizontal, 1 diagonal,
2 not stacked (Autodesk help DIMLUNIT, DIMFRAC; ObjectARX `AcDmUtil::dimunit/dimfrac`). `287 4` is real in AutoCAD's R14 as a DSTYLE override (`bl15`: `1070 287 1070 4`).

| 270 DIMUNIT | → `dimlunit`, `dimfrac` | | `dimlunit`, `dimfrac` → 270 |
|---|---|---|---|
| 1 | 1, 0 | | (1, any) → 1 |
| 2 | 2, 0 | | (2, any) → 2 |
| 3 | 3, 0 | | (3, any) → 3 |
| 4 stacked | 4, 0 | | (4, 0 or 1) → 4 |
| 5 stacked | 5, 0 | | (5, 0 or 1) → 5 |
| 6 | 4, 2 | | (4, 2) → 6 |
| 7 | 5, 2 | | (5, 2) → 7 |
| 8 (Windows desktop) | 6, 0 | | (6, any) → 8 |
| other | keep the number, warning | | – |

| 287 DIMFIT | → `dimatfit`, `dimtmove` | | `dimatfit`, `dimtmove` → 287 |
|---|---|---|---|
| 0–3 | n, 0 | | (n, 0) → n |
| 4 | 3, 1 | | (3, 1) → 4; (other, 1) → error |
| 5 | 3, 2 | | (3, 2) → 5; (other, 2) → error |

Reading 270/287 sets the modern fields by the table and **keeps the legacy value as read** (typed field with presence: a file's `287 4` must come back as `4`); a changed
modern field recomputes the legacy value. The numbers come from Autodesk's ObjectARX `AcDmUtil` functions as the search summary read them ("0 to 3 when DIMTMOVE is 0, 4 when 1, 5 when 2";
the pages do not load) and are **checked by AutoCAD's core in 7.24** (read a file with 270 4–7 and 287 4/5 and print `DIMLUNIT`, `DIMFRAC`, `DIMATFIT`, `DIMTMOVE`: open question 3).
Modern settings with no legacy form (DIMADEC, DIMDSEP, DIMTXTDIRECTION, DIMLDRBLK, linetypes, lineweights, DIMFXL, DIMJOGANG, DIMTFILL, DIMARCSYM, DIMALTRND, DIMAZIN, DIMFRAC≠0 with
DIMLUNIT not 4/5) at a non-default value are version errors (B91), at the default simply not written.

## 9. OBJECTS

* The first object is the **root named-object dictionary** (R14:object_ownership: "always the first object that appears in this section"): `0 DICTIONARY`, `5`, `100 AcDbDictionary`, then `3 <name>` + `350 <handle>` per entry. It has **no
  reactor group** and no `330` ("persistent reactors group appears in all dictionaries except the main dictionary": R13:Common Object Group Codes, R14:group). Every other dictionary: `5`, `102 {ACAD_REACTORS`, `330 <owner dictionary>`, `102 }`, `100 AcDbDictionary`, entries.
  **No `280` (hard owner) and no `281` (cloning)** in R13/R14 DICTIONARY — none of the files has them in family N; **no `280` in XRECORD**. The 2000 writer's automatic `281`/`280` is wrong here.
* **New drawing:** root dictionary (`C`) with `ACAD_GROUP` → empty dictionary (`D`, reactor `330 C`) and `ACAD_MLINESTYLE` → dictionary (reactor `330 C`) with `STANDARD` → MLINESTYLE; nothing else (`az`, `head`'s toolbox
  OBJECTS in `simp`, `t3`, `dxfp`, `rm2`). The `ACDBVARIABLEDICTIONARY` (+ `DICTIONARYVAR` objects: `100 DictionaryVariables`, `280 0`, `1 <value>`, R14:dictionaryvar) exists when a variable
  of R14:dictionaryvar's list (`DIMADEC`, `DIMDSEP`, `INDEXCTL`, `PROJECTNAME`, `XCLIPFRAME`) has been set (`v13`: `DIMADEC 0`). No `ACAD_LAYOUT`, `ACAD_PLOTSETTINGS`, `ACAD_PLOTSTYLENAME`, `ACAD_MATERIAL`, `ACAD_VISUALSTYLE`.
  Family A2000's skeleton (`s13`: `ACAD_PLOTSETTINGS`, `ACAD_LAYOUT`, XDATA `1001 ACAD`, `1000 TREAT_AS_HARD`, `1070 0` on the root and the group/mlinestyle dictionaries, `ACAD_LAYOUTSELFREF` + XRECORD `340`
  in the space blocks' extension dictionaries) is kept when read and not created.
* **MLINESTYLE** `STANDARD`: `0 MLINESTYLE`, `5`, `102 {ACAD_REACTORS` `330 <the ACAD_MLINESTYLE dictionary>` `102 }`, `100 AcDbMlineStyle`, `2 STANDARD`, `70 0`, `3` (empty), `62` fill colour,
  `51 90.0`, `52 90.0`, `71 2`, then per element `49` offset, `62` colour, `6` linetype: `0.5 / 256 / BYLAYER`, `-0.5 / 256 / BYLAYER` (R13:MLINESTYLE). The fill colour: **256** in `v13`, `s13`, `qdx`, `alt`, `t3` LeachTank
  (R14:mlinestyle "default = 256"), **0** in `az`, `dxfp`, `mii`, `rm2`, `t3` FourPlusUnits (the AutoCAD of that time often wrote 0): new drawings write 256.
* **GROUP**: `0 GROUP`, `5`, `102 {ACAD_REACTORS` `330 <ACAD_GROUP dictionary>` `102 }`, `100 AcDbGroup`, `300` description, `70` unnamed flag, `71` selectable flag, `340 <member>` per entity (R14:group;
  `Glock-o-bot` GROUP 31: `300 Group created by Visio.`, `70 0`, `71 1`, 24 members).
* **XRECORD**: `100 AcDbXrecord` + the application's groups (R14:xrecord). **Not typed beyond what B12 keeps**: IDBUFFER, LAYER_INDEX, SPATIAL_INDEX, SPATIAL_FILTER, SORTENTSTABLE, OBJECT_PTR, ACAD_PROXY_OBJECT,
  ACDBDICTIONARYWDFLT, ACDBPLACEHOLDER, LAYOUT, PLOTSETTINGS … (phase 9; IMAGEDEF and its objects are typed in 7.15).
* **IMAGEDEF, IMAGEDEF_REACTOR, RASTERVARIABLES, `ACAD_IMAGE_DICT`/`_VARS`** (R14): `ACAD_IMAGE_DICT` → a DICTIONARY (reactor `330 <root>`) of name → IMAGEDEF; `ACAD_IMAGE_VARS` → the RASTERVARIABLES handle directly
  (`alt-yeager-io`: root `3=ACAD_IMAGE_VARS 350=FAE4982DDDE83E3A`; the R14 reference says its owner is "the ACAD_IMAGE_VARS entry"). IMAGEDEF: `102 {ACAD_REACTORS` `330 <ACAD_IMAGE_DICT>` `330 <IMAGEDEF_REACTOR>…` `102 }`,
  `100 AcDbRasterImageDef`, `90 0`, `1` path, `10 20` size in pixels, `11 21` default pixel size, `280` loaded flag, `281` resolution units (0 none, 2 cm, 5 inch). IMAGEDEF_REACTOR: `5`, `100 AcDbRasterImageDefReactor`,
  `90 2`, `330 <image>`. RASTERVARIABLES: `5`, `102 {ACAD_REACTORS` `330 …` `102 }`, `100 AcDbRasterVariables`, `90 0`, `70` frame, `71` quality, `72` units (R14:rastervariables, revised u14.1.02).
* Handles, reactors and ownership follow B19/B97: group members and hatch boundaries are soft pointers; deleting an entity removes its group membership.

## 10. ACIS

* Entity: `100 AcDbModelerGeometry`, **`70 1`** ("Modeler format version number (currently = 1)": R14:3dsolid, body, region; R13 the same), then the SAT text in `1` groups (**one SAT record per group**, < 255 characters:
  maxima 82, 194, 231 characters in `example_r13`, `as_flat`, `kicad`), `3` for a continuation only when a `1` string would exceed 255. No `AcDb3dSolid` marker, no history `350`, no `2` GUID, no ACDSDATA/SAB.
* **Encryption**: every character except the space is replaced by `159 − c` (HDxf:`src/acis.cpp` already decodes this for 2000+). The decoded text: line 1 = the header `106 <records> 1 0`
  (version, number of records, 1, 0), then **no product/date/units lines**: the records follow at once, `body $-1 $1 $-1 $-1 #`, `lump …`, `shell …`, `face …`, `loop …`, `plane-surface $-1 x y z nx ny nz ux uy uz …`,
  each ended by `#`, no `End-of-ACIS-data`, doubles with 17 digits (decoded from `example_r13.dxf` REGION 176, `kicad` 93A0).
* **SAT version per DXF version.** Seen: `106` in ODA's **R13 and R14** output of AutoCAD's 2000-form bodies (400 in `example_2000.dxf`, the source) — `example_r13`/`example_r14` REGION 176, 3DSOLID 2E1, REGION 37D;
  `106` in R14 files `as_flat`, `kicad`. No AutoCAD-origin R13/R14 file has a solid. The plan's "R13 = 106, R14 = 400" is **wrong for 400**: 400 is ACIS 4.0, AutoCAD 2000's; the Migration Guide (read by the
  plan, not re-readable today) puts R13–13c3 at ACIS 1.5, 13c4/R14 at 1.6 (= 106) and R14.01 at 3.0. **Rule (D11):** an R13 or R14 body is written with the SAT it has when that SAT's version
  is **105 or 106** (and 300 for R14, read from a file only); any other version (2000's 400, 20800 … 22300, ODA's 700) is an error `"ACIS SAT version N cannot be written as DXF R13"`/`"…R14"` (B95);
  a body that has only SAB (2013+) gives `"ACIS SAB cannot be written as DXF R13"`/`"…R14"`. HDxf converts nothing between SAT versions and writes no SAB for R13/R14. A body read from an R13/R14
  file comes back byte for byte (B12).
* HDxf has **no 106 writer**: bodies enter R13/R14 only as read, or with SAT 106 lines supplied by the program (`libs/hdxf_acis` writes 21500 only). R14's CLASSES have **no record for 3DSOLID/REGION/BODY**
  (R14:classes_section "Default class values": the modeler entities are built in).
* 2013's `AcisData::guid`, `sab`, the modeler `70` > 1 and the `ASM` product lines do not exist.

## 11. Binary DXF

* Sentinel: `AutoCAD Binary DXF<CR><LF><SUB><NUL>` (22 bytes), strings NUL-terminated, 16-bit values little-endian, doubles 8 bytes (R13:Binary DXF File Format, R14:binary_dxf_file_format).
* **Group code width.** R13 and R14 references: "The group code is a single-byte binary value", extended data codes as `255` + a 2-byte code (R13/R14 binary pages). The **2005 reference** (AutoCAD 2005 DXF Reference,
  Appendix A, "Binary DXF File Format"): "The group code is a 2-byte binary value (1 byte in DXF files prior to AutoCAD Release 14)", extended data `255` + 2-byte code. The only R13/R14 binary files that exist
  (`b13`, `b14`, trimesh's `r13b`/`r14b`) are ODA's: two-byte codes in R13 as in R14. **No AutoCAD-saved binary R13/R14 file exists.** Hence: **R14: two-byte codes** (2005 reference, ODA's files, HDxf's 2000 codec);
  **R13: not decided by any AutoCAD file** — the R13 reference says one byte, but R13 introduced the codes 280–369 (`330`, `340`, `350`…), which a single byte cannot hold and the reference's escape covers only 1000+.
  Decision (D12): 7.4 implements both widths for R13 behind one switch, writes **two-byte codes for R13** (as ODA, and the width needed by the codes 280–369) and **7.24 reads both with `accoreconsole` and ODA**; if AutoCAD's core
  refuses the two-byte R13, single-byte-with-escape is the form (open question 1).
* **Value widths**: `280–289` are 16-bit integers in R13/R14 binary as in 2000 (`b13`, `b14`; the plan's B88), `90–99` 32-bit, `1071` 32-bit, `1004`/`310–319` a length byte + data; `999` is never written.
* EOF: `0 EOF` as a record (all binary files).

## 12. Target matrix

N = native form of this profile, H = older helper form (the old representation), E = version error. "Step" = plan step. Version-error texts follow B94/B95/B98.

| Part | R13 | R14 | Decision and source | Step |
|---|---|---|---|---|
| Version selection, dispatch | N | N | B86; `$ACADVER` AC1012/AC1014 | 7.2 |
| HEADER variables, order, codes | N (162, `$SAVEIMAGES` last, `$BLIPMODE`) | N (164) | sections 3.1–3.2 | 7.3, 7.4 |
| Header values of a new drawing | N | N | D2; maintenance: none (R13), 0 (R14) (D3) | 7.3, 7.4 |
| Header 2000-only variables | E | E | B98 | 7.3, 7.20 |
| Binary DXF | N, two-byte codes (D12, to be checked) | N, two-byte codes | section 11 | 7.4 |
| CLASSES | N, list as used, `90` 32768 | N, R14's 15 | D4, section 4 | 7.10 |
| Table heads, counts | N, no `330` | N | D5, section 5.1 | 7.6 |
| VPORT, LTYPE, LAYER, STYLE, VIEW, UCS, APPID | N | N | section 5.2; names upper case | 7.6 |
| BLOCK_RECORD | N (name only) | N | section 5.2 | 7.6 |
| DIMSTYLE | N (all groups, 105, 5/6/7 names, 270, 287, 340) | N | section 8; mapping D10 | 7.7, 7.8 |
| Space blocks, BLOCK/ENDBLK | N, `*MODEL_SPACE`/`*PAPER_SPACE` | N | section 6 | 7.5, 7.11 |
| Paper space: one space, VIEWPORT old form | N | N | D6; 6.1, 6.2 | 7.5, 7.16 |
| Layouts, LAYOUT, plot settings | E (`"LAYOUT requires DXF 2000 or later"`) | E | B89 | 7.5, 7.20 |
| Entity head | N, no `330`/`370`/`380`/`390` | N | 7.1 | 7.11 |
| LINE, POINT, CIRCLE, ARC, TRACE, SOLID, 3DFACE, SHAPE, TEXT, ATTDEF, ATTRIB, INSERT, SEQEND | N | N | 7.2 | 7.11 |
| POLYLINE/VERTEX/SEQEND (2D, 3D, mesh, polyface) | N | N | 7.2 (`AcDbPolygonMesh`, polyface: open question 10) | 7.13 |
| DIMENSION (all types) + `*D` blocks | N, `70` +32, no `71`/`42` | N | 7.3 | 7.12, 7.16 |
| VIEWPORT | N (R12-like + MVIEW) | N | 6.2 | 7.16 |
| ELLIPSE, XLINE, RAY, SPLINE, TOLERANCE | N | N | 7.4 | 7.12 |
| MTEXT | N (`72` 1–4, chunks 255) | N (`72` 1/3/5, chunks 250, 42/43/50) | 7.4 | 7.12 |
| LEADER | N (`212`) | N (`212`, `213`) | 7.4 | 7.12 |
| MLINE | N (`71` 1/3) | N (`71` bits) | 7.4; MLINESTYLE section 9 | 7.12, 7.9 |
| LWPOLYLINE | E `"LWPOLYLINE requires DXF R14 or later"` | N | D7 | 7.13 |
| HATCH | E `"HATCH requires DXF R14 or later"` | N | D8 | 7.14 |
| IMAGE, IMAGEDEF, IMAGEDEF_REACTOR, RASTERVARIABLES | E `"IMAGE requires DXF R14 or later"` | N | D9 | 7.15 |
| 3DSOLID, REGION, BODY | N with SAT 105/106; else E | N with SAT 106 (300 read); else E | D11, section 10 | 7.17 |
| SAB/ACDSDATA | E | E | section 10 | 7.17, 7.20 |
| DICTIONARY, GROUP, MLINESTYLE, DICTIONARYVAR, XRECORD | N, no `280`/`281` | N | section 9 | 7.9 |
| Other objects (proxy, index, filter, sortents …) | as read | as read | B92; phase 9 types | 7.9, 7.19 |
| Handles, owners, reactors, `$HANDSEED` | N | N | B97 | 7.11, 7.19 |
| Lineweight, plot style, transparency, material, shadow, true colour, 2000+ entity types | E | E | B98 (`"Lineweight requires DXF 2000 or later"`, …) | 7.20 |
| Symbol names | upper case (read: as read); 31-character limit and characters: see below | same | names in all 27 files; R13/R14 limit not found in the references (`$DIMPOST` strings 255) | 7.21 |
| Text encoding | code page bytes; characters outside it as `\U+XXXX` (`bl15` MTEXT `\A1;\U+220512.00/h8\PINPUT`: the diameter sign in AutoCAD 2000 as R14) | same | B1/B55; no AutoCAD-origin R13 file has a non-ASCII byte or `\U+` | 7.21 |
| Read file → own version, B12 | N, family kept | N | D1 | 7.18, 7.19 |

**Symbol name length.** Neither reference states the 31-character limit of R13/R14; R13:HEADER/`$CMLSTYLE` and MLINE "String of up to 32 characters" (the mline style name, R13:MLINE) is the only length in the text, and
the APPID name "ASCII string up to 31 bytes long" (R13:1001 group). AutoCAD R13/R14 limited symbol names to 31 characters (documented in the Command Reference, not available here): open question 13.

## 13. Deviations between references and files

| Id | Reference | Files | Decision |
|---|---|---|---|
| D13.1 | R13:HEADER `$ACADVER` "AC1011 in R13" | AC1012 | AC1012 |
| D13.2 | R13:HEADER `$DIMBLK` code 2; `$DIMTXSTY` "70" | 1 and 7 | files, R14 reference |
| D13.3 | R13/R14 binary: single-byte codes | ODA's R13/R14: two-byte; 2005 reference: two bytes from R14 | section 11 |
| D13.4 | R13:BLOCKS "paper space entities … followed by model space entities" | AutoCAD's R14 `puppenhaus_4mm` has the VIEWPORTs after the model entities; the other files with both put them last (not AutoCAD) | R13: the reference (D6); R14: the file (list order) |
| D13.5 | R14:classes_section lists the application column unquoted | AutoCAD writes `"AutoCAD"`, `"Acad"`, `"<Built In>"` with quotes | files |
| D13.6 | R14:dimstyle `176` "DIMDLRD" | DIMCLRD | DIMCLRD |
| D13.7 | R13/R14:POLYLINE/VERTEX: `AcDb2dPolyline`/`AcDb3dPolyline`, `AcDb2dVertex`/`AcDb3dPolylineVertex` only | `AcDbPolygonMesh`/`AcDbPolygonMeshVertex` (`pyr`) | files |
| D13.8 | R14:dimension pages: angular `AcDb3PointAngularDimension` for both angular types | no sample; AutoCAD 2000+: `AcDb2LineAngularDimension` for type 2 | reference (open question 6) |
| D13.9 | R13:MLINESTYLE "Fill color … default = 256" is R14's text | R13-era files: 62 = 0 | 256 for new records |
| D13.10 | R13:Common object group codes: reactor owner "dictionary" `330` | the same in all files | as reference |
| D13.11 | the plan's D7.5 / B95 "R14 = SAT 400" | 106 | section 10 |
| D13.12 | the plan's B88 "new R14 `$ACADMAINTVER` 70 9" | native R14: 0 / 8 | 0 (D3) |
| D13.14 | R14:header_section DIMUNIT/DIMALTU 1–7 | `270 8`, `273 8` in `bl15` (AutoCAD 2000 as R14) | 8 = DIMLUNIT 6 (D10) |
| D13.15 | R14:classes_section: AutoCAD registers 14 default classes | AutoCAD writes the classes the drawing uses (`ca`: 1, `bl15`: 2, `pup`: 3) or all 15 (`t3`) | used classes (D4) |
| D13.16 | R14:hatch: `47` pixel size listed without a condition | AutoCAD 2025's core (1.8.1, variants of one file) reads `47` after `76` **exactly when a boundary path is derived** (`92` bit 4): "expected group code 47" when a derived path has none, "expected group code 98" when a hatch without a derived path has one; seed points (`98`) are independent. AutoCAD's own R14 files agree (`bl15`: `92 7` + `47`; `BenF18`, `FYP`, `bne`: `92 3`, no `47`) | R13/R14: `47` iff a path has bit 4 |
| D13.17 | – | AutoCAD writes a shape file's STYLE with **no name** (`2` empty, `70 1`, `3 ltypeshp.shx`: `altera_*`, `puppenhaus_4mm`, `QDXDivide`); AutoCAD 2025 refuses a named one ("Invalid textstyle name for shapeFile") | the program names a shape-file style as AutoCAD: empty |
| D13.18 | R13/R14 references: angular dimension `AcDb3PointAngularDimension` for both (open question 6) | AutoCAD 2025's core reads type 2 only with `AcDb2LineAngularDimension` (1.8.0) | type 2 `AcDb2LineAngularDimension`, type 5 `AcDb3PointAngularDimension` |

## 14. Open questions (what is missing, concretely)

No item needs a file only Erik can make: AutoCAD cannot write R13/R14 DXF. They are settled by **7.24's core read** (`accoreconsole` of DWG TrueView 2027 reads old DXF; ODA reads it) or are choices with no reader impact.

1. **Binary R13 group-code width.** One byte (references) or two (ODA; codes 280–369). Settle in 7.4: write both, read both with the core and ODA (section 11).
2. Order of `{ACAD_REACTORS` and `{ACAD_XDICTIONARY` on R13/R14 records (the reference's: reactors first). No AutoCAD-origin record has an extension dictionary.
3. DIMUNIT/DIMFIT mapping (section 8): read a file with 270 4–7 and 287 4/5 with the core and compare `DIMLUNIT`/`DIMFRAC`/`DIMATFIT`/`DIMTMOVE`; in particular stacked fractions (DIMFRAC 0 or 1).
4. ENTITIES order of paper space and model space: **settled for R14** by AutoCAD's own `puppenhaus_4mm` (paper space VIEWPORTs last, list order; D6); R13 (paper first, the reference) has no file, so 7.24's core read confirms it.
5. The old VIEWPORT with `ACAD`/`MVIEW` XDATA is AutoCAD's own in R14 (`pup`: `67 1`, `10 20 30`, `40`, `41`, `68 1`, `69 1`, MVIEW); R13: reference only. Confirm in 7.24 that the core builds the viewport from it.
6. Angular dimension subclass: `AcDb3PointAngularDimension` for type 2 (reference) or `AcDb2LineAngularDimension` (2000+).
7. Names of the built-in arrow blocks in R13/R14 DIMBLK (5, 6, 7): `_DOT`-style (what HDxf builds for 2000) or `DOT`; none of the 27 files has a non-empty DIMBLK.
8. TrueType text styles in R13: no sample (R14: `bl15` `3 ARIBLK.TTF` + XDATA `ACAD`, section 5.3).
9. SAT 105 for R13 and 300 for R14.01: no sample; only 106 is seen.
10. POLYLINE as polyface mesh (`AcDbPolyFaceMesh`, `AcDbFaceRecord`) in R13/R14: no sample in any AutoCAD-origin file (the mesh form `AcDbPolygonMesh` is in `pyr`).
11. The IMAGE entity record in R14: the reference only (`alt-yeager-io` has the objects, the entity was deleted).
12. MLINE flags, MTEXT `72`, LEADER `212/213` per release: reference only, no AutoCAD-origin record.
13. The symbol-name length limit (31) and the characters R13/R14 allow: not in the references found.
14. The Autodesk Migration Guide's ACIS section was not re-read (bot challenge on 2026-10-27): the SAT mapping rests on the plan's quotation and on ODA's files.

## Appendix: how the files were checked

Small scripts of our own (Python, standard library) read each file to the end as code/value pairs (binary: with the width the file uses) and listed header variables, tables with declared and actual counts,
entity and object types, CLASS records, and the first record of each type. Nothing was produced by HDxf, nothing was converted. The ASCII files' SHA256 and origin are in section 1.4 and in
`test/data/manifest.cmake`/`test/data/README.md`; the local-only copies in `build/test-data/local-r13r14/PROVENANCE.txt`.

# HDXF - DXF File Library

## Project Overview
HDXF is a C++ library for reading, writing, and manipulating DXF (Drawing Exchange Format) files.
The library supports the full DXF standard including 2D and 3D entities, text, colors, layers,
blocks, styles, and all standard DXF sections. Both ASCII and Binary DXF formats are supported.

**Version: 1.9.1 — reads R12 … 2018 (AC1009–AC1032, ASCII and binary), writes R12, R13, R14, 2000,
2004, 2007, 2010, 2013 and 2018. Phases 0–7 and 9 of the multi-version work (TODO.md) are done; phase 8 remains.**
R12: all 14 entity types, 8 table types, header variables, blocks, attributes, dimensions,
viewports, XDATA, and both ASCII/Binary formats pass AutoCAD validation. 2000: the R12
types plus LWPOLYLINE, ELLIPSE, XLINE, RAY, MTEXT, TOLERANCE, LEADER, SPLINE, HATCH, MLINE,
IMAGE and 3DSOLID/REGION/BODY, OBJECTS, BLOCK_RECORD and layouts, checked in DWG TrueView
and ODA File Converter (plan `doc/plans/2026-10-23-fas-1-dxf-2000.md`, B10). 2004: true
color, color books (DBCOLOR), transparency, MTEXT background, gradient fills, shade plot,
THUMBNAILIMAGE (plan `doc/plans/2026-10-23-fas-2-dxf-2004.md`, B20–B29).

Open source under the MIT license ([LICENSE](LICENSE)); since 1.9.1 there is no license check, and all
file I/O works without one. `README.md` is the English overview, `README.sv.md` the full description in
Swedish. `.github/workflows/release.yml` builds the prebuilt packages (MSVC, MinGW, Linux GCC) for every
tag `vX.Y.Z`; `HDXF_INSTALL` gives `cmake --install` and the CMake package `find_package(hdxf)`.

## Architecture
- Single public header: `include/hdxf.hpp`
- `Drawing` class as the main facade — all operations go through it
- Entities use `std::variant` (value types, no inheritance/pointers)
- Internal headers in `src/` are not exposed to users
- Unknown entities/objects preserved as raw group codes for round-trip safety

## Build System
- **Build tool**: CMake (minimum 3.20)
- **Generator**: Ninja
- **Compiler**: MSYS2/UCRT64 (GCC)
- **Standard**: C++20
- **Dependencies**: None (C++ standard library only)

## Project Structure
```
HDXF/
├── CLAUDE.md              # This file
├── TODO.md                # Multi-version plan: phases 0–9 (0–7 and 9 done, 8 remains)
├── CMakeLists.txt         # Root CMake configuration
├── include/
│   └── hdxf.hpp           # Single public header (all public API)
├── src/
│   ├── CMakeLists.txt     # Library build
│   └── *.cpp/hpp          # Implementation files (internal)
├── libs/
│   └── hdxf_acis/         # ACIS body builder (own CMakeLists, include/, src/, test/, tools/)
├── test/
│   ├── CMakeLists.txt     # Test build
│   ├── test_framework.hpp # Minimal test framework (no dependencies)
│   ├── main.cpp           # Test runner entry point
│   └── test_*.cpp         # Test files
└── doc/
    ├── README.md
    ├── acad_*_dxf.dxf     # AutoCAD 2025's files, R12 … 2018 (round-trip facit)
    ├── facit/             # AutoCAD scripts that make the facit (the files themselves are not in git)
    ├── plans/             # The phase plans and their outcomes
    └── reference/         # HDxf's own format notes (R13/R14, ACIS SAT 21500)
```
Autodesk's DXF references (R12, 2005 … 2014, 2018) are not in the repository; they are on Autodesk's
site and are cited by version and page.

## Building

### Prerequisites
- MSYS2 with UCRT64 environment
- CMake 3.20+
- GCC (via UCRT64)

### Build Commands
```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DCMAKE_BUILD_TYPE=Release
cmake --build build
python -I tools/run_tests.py
```
The full suite is `tools/run_tests.py`: `hdxf_tests --shard=K/N` in parallel shards, each with its own TMP, the
results summed (filters as for `hdxf_tests`; `-j N` shards). Without a build type the build is Release; a Debug
build runs the suite many times slower. `./build/test/hdxf_tests [filter …]` runs tests in one process.

### No golden files
HDxf has no golden files (Erik, 2026-10-25): the only facit is a file AutoCAD itself has saved
(`build/test-data`, listed in `test/data/manifest.cmake`). What has no AutoCAD file is listed
in the phase plan under "Facit som Erik skapar i AutoCAD". ODA File Converter, ezdxf and
`accoreconsole` (DWG TrueView's core) are used to check what HDxf writes, never as facit.

### Test data
External DXF files (ACadSharp, ezdxf, jscad, Aspose) are not checked in: `test/data/manifest.cmake` lists
them (URL pinned to a commit + SHA256) and configure downloads them to `build/test-data/`
when missing. `-DHDXF_TEST_DATA=OFF` turns that off; their tests are then skipped. Facit
and sources: `test/data/README.md`. Own small files: `test/data/synthetic/` (checked in).

## Branches and Releases
- Single branch: `main` (no `dev`/`master`) — commit directly to it
- Releases are tags `vX.Y.Z` on `main`; a program that uses HDxf pins a tag or commit, never a branch
- Multi-version work (TODO.md): each finished phase is a release and bumps the minor
  version (phase 0 → 1.1.0, phase 1 → 1.2.0 …); sub-steps (0.1, 0.2 …) are committed
  and pushed
  without a tag; fixes after a release bump the patch (1.1.1 …); update the version in
  `CMakeLists.txt`, `include/hdxf.hpp` (`struct Version` and the header comment; `version_string` checks it against
  CMakeLists.txt), `CLAUDE.md`, `README.md` and `README.sv.md`, and tag it;
  2.0.0 when all phases are done

## Using in Other Projects (CMake FetchContent)
```cmake
include(FetchContent)
FetchContent_Declare(
    hdxf
    GIT_REPOSITORY https://github.com/holmbom/HDxf.git
    GIT_TAG v1.9.1
)
FetchContent_MakeAvailable(hdxf)

target_link_libraries(your_target PRIVATE hdxf::hdxf)
```

## API Design
- File paths are `std::filesystem::path` — never `std::string`. A narrow
  `std::ifstream(std::string)` goes to `fopen` and is ANSI-interpreted on Windows, so
  non-ASCII paths are lost. Use `hdxf::path_from_utf8()` / `hdxf::utf8_from_path()`
  (both in `include/hdxf.hpp`) when converting to and from UTF-8 `std::string` —
  `fs::path(str)` / `.string()` are UTF-8 only on libstdc++
- Every string in the API is UTF-8 (plan B1, since 1.1.0). The reader decodes
  `$DWGCODEPAGE` (≤ 2004) / UTF-8 (2007+), `\U+XXXX` and ASCII caret notation (`^J`);
  the writer encodes back (`src/text_encoding.cpp`). Records preserved from 2000+ files
  (`RawRecord`) keep the bytes as written
- `Drawing::warnings()` lists what a successful load/save had to change or could not do (e.g.
  a font not found); cleared by every I/O call like `last_error()`. A character above U+FFFF
  is written as its UTF-16 surrogates in two `\U+` escapes in every version — the only form
  AutoCAD reads it in (plan phase 4, B55)
- ASCII line endings: a read file is written back with its own (CRLF in, CRLF out; LF in, LF out); a drawing
  built in memory gets CRLF, as AutoCAD writes it. `doc/acad_*_dxf.dxf` (AutoCAD 2025, R12 … 2018) are
  the facit: `test/test_autocad_facit.cpp` requires a byte-identical round trip of each
- `save()`/`save_as()` write the whole file to memory first — a failure never leaves a
  partly written file
- `Drawing::load(path)` reads a file and remembers the path
- `Drawing::save()` writes back to the loaded file
- `Drawing::save_as(path)` writes to a new file and updates the stored path
- `Drawing::load_from_memory(data, size)` reads from a `uint8_t` buffer (no filename set — `save()` will fail)
- `Drawing::save_to_memory(format)` returns file content as `std::vector<uint8_t>`
- `Drawing::remove_layer(name)` removes a layer and moves its entities to layer "0" (layer "0" cannot be removed)
- Mutable accessors (`layers()`, `entities()`) allow direct modification after load
- Memory-based I/O uses the same DxfReader/DxfWriter internally — both ASCII and Binary formats work
- All I/O methods clear `last_error()` on entry and set it on every failure path, so it is
  empty after a successful call
- DXF 2000 (since 1.2.0, plan `doc/plans/2026-10-23-fas-1-dxf-2000.md`, B11–B19):
  `Drawing()` stays R12; `Drawing(DxfVersion::R2000)` / `set_version(R2000)` gives a 2000
  drawing with AutoCAD's structure. A drawing read from 2000 is written back from its
  preserved records (`RawRecord`) with only what changed written anew; lists are written
  whole; relations (owners, reactors, groups, dictionaries, draw order, block references,
  leader/hatch/image links) follow removals. 2000-only entity types fail as R12 with
  `"<TYPE> requires DXF 2000 or later"`. OBJECTS are in `objects()`, BLOCK_RECORD in
  `Block::record`, layouts in `layouts()`
- DXF 2004 (since 1.3.0, plan `doc/plans/2026-10-23-fas-2-dxf-2004.md`, B20–B29): the
  2000 writer with the version deciding what differs; `Drawing(DxfVersion::R2004)`. A drawing
  read from 2000 or 2004 is written only as that version (converting is phase 8). `Color`
  has `rgb` (420) and `name` (430) besides `aci`; `nearest_aci()` is AutoCAD's choice. What
  only 2004 has fails as R12/2000 with `"<what> requires DXF 2004 or later"`.
  `Transparency` on entities (440) and layers (XDATA), `Layer::xdata`, `thumbnail()`
- DXF 2007 (fas 3, plan `doc/plans/2026-10-24-fas-3-dxf-2007.md`, B30–B41): the 2000/2004
  writer with the version deciding what differs; `Drawing(DxfVersion::R2007)`. Text in UTF-8;
  a new drawing has AutoCAD's materials and visual styles (B34). A drawing read from 2000,
  2004 or 2007 is written only as that version. What only 2007 has fails as older with
  `"<what> requires DXF 2007 or later"` (materials, visual styles, MTEXT's defined height, new
  surfaces …). MULTILEADER (`Boxed<MLeaderData>`) and MLEADERSTYLE are typed and written in
  2000/2004/2007 with AutoCAD's proxy graphics (`src/mleader_graphics.cpp`); `add_mleader()` /
  `add_mleader_block()` place the content as AutoCAD. Surfaces (`SurfaceData`, written anew only
  in 2007) and HELIX (`HelixData`) are typed; their extents are AutoCAD's (ACIS, the helix's
  polyline). TrueType text height is the top of the glyph "A" (R.8 corrected in 3.4)
- DXF 2010 (fas 4, plan `doc/plans/2026-10-24-fas-4-dxf-2010.md`, B42–B55): the 2007 writer
  with the version deciding what differs; `Drawing(DxfVersion::R2010)`. A drawing read from
  2000 … 2010 is written only as that version. What only 2010 has fails as older with
  `"<what> requires DXF 2010 or later"` (IMAGE's `clip_inside`, DIMTXTDIRECTION, vertical
  MULTILEADER text, a leader line's own properties, hatch spline fit data, GEODATA v2/v3).
  MESH (`Boxed<MeshData>`, `add_mesh()`) is typed and written in 2000 … 2010; its extents and
  proxy graphics follow AutoCAD's smoothing (`src/mesh.cpp`, Catmull-Clark with AutoCAD's crease
  rules, B49). GEODATA (`GeoDataData`, `set_geodata()`) is typed in its three versions.
  `MLeaderLine` has the 2010 line properties typed (`newer_groups` is gone). Characters above
  U+FFFF are written as two `\U+` surrogate escapes in every version (B55). IMAGE gets
  AutoCAD's outline proxy graphics in 2000 … 2010
- DXF 2013 (fas 5, plan `doc/plans/2026-10-24-fas-5-dxf-2013.md`, B56–B68): the 2010 writer
  with the version deciding what differs; `Drawing(DxfVersion::R2013)`. A drawing read from
  2013 is written only as 2013 (ACadSharp's sample byte for byte). Typed: `$REQUIREDVERSIONS`,
  `Layer::pointer_348`, the linetype's upright (74 bit 8), `MLeaderStyleData::text_extended`
  (298), `AcisData::guid` (290/2) and the section ACDSDATA (`src/acds.cpp`,
  `src/dxf_writer_acds.cpp`: `AcisData::sab`, `LayoutData::thumbnail`, `View::thumbnail`; no
  longer in `raw_sections()`). ACIS in 2013 is SAB 21800: a body with SAT lines (any version
  1.0 … 21800) is written as AutoCAD writes it (`src/acis_sab.cpp`, B62/B63; AutoCAD's own SAB
  record for record, MASSPROP as AutoCAD's). 2013's properties go in 2000 … 2010 as AutoCAD
  writes them (round-trip XRECORDs, XDATA `ACAD_OBJECT_CHANGE_GUID`; `src/drawing_roundtrip.cpp`,
  B60). What only 2013 has fails as older (`"... requires DXF 2013 or later"`: a body with only
  SAB, LAYER 348, GEODATA v3). A record written anew keeps what AutoCAD's has (5.9): the reactors
  as read (`EntityProperties::reactors`, `DimStyle::reactors`), extension dictionaries and XDATA
  of the table entries, BLOCK's properties and description, BLOCK_RECORD's preview, 380
- DXF 2018 (fas 6, plan `doc/plans/2026-10-25-fas-6-dxf-2018.md`, B69–B85): the 2013 writer with
  the version deciding what differs; `Drawing(DxfVersion::R2018)`. A drawing read from 2018 is
  written from its preserved records (ACadSharp's two samples byte for byte); any other
  version of a read drawing fails with `"Writing DXF X as Y is not supported yet ..."`.
  `$ACADMAINTVER` is `90` (`maintenance_as_written()`), `$REQUIREDVERSIONS` int64. `101 Embedded
  Object` after the parent's fields: MTEXT columns (`MTextData::columns`, `MTextColumns`,
  `MTextEmbedded`; before 2018 XDATA `ACAD_MTEXT_COLUMN_INFO` + linked MTEXT + frame
  LWPOLYLINEs, `src/mtext_data.cpp`, `drawing_mtext.cpp`) and multiline ATTRIB/ATTDEF
  (`MultilineAttribute`; before 2018 XRECORD `ACAD_MLATT`/`ACAD_MATT_HEIGHT` + the TEXT mirror,
  `drawing_attribute.cpp`, `attribute_mirror.cpp`); an embedded part HDxf does not type stays in
  `RawRecord`. `ModelDocViewStyleData` (DETAIL/SECTION view styles, `drawing_object_fields.cpp`),
  coordination model + NAVISWORKSMODELDEF + RAPIDRT settings/environment (`dxf_writer_reference.cpp`,
  B78, as the reference). ACIS: SAT → SAB 22300 (`sab_format_for()`, `src/acis_sab.cpp`); a read SAB is
  written with its bytes. `OptionalBoxed<T>` keeps `sizeof(Entity)` (B84). No golden files, and
  a facit is only a file AutoCAD saved (Erik, 2026-10-25); what has none is listed in the plan
- DXF R13 and R14 (fas 7, 1.8.0, plan `doc/plans/2026-10-25-fas-7-dxf-r13-r14.md`, B86–B99, format in
  `doc/reference/hdxf_r13_r14_format.md`): `Drawing(DxfVersion::R13/R14)` writes AC1012/AC1014, ASCII and binary.
  No AutoCAD saves R13/R14 any more: the facit is AutoCAD-saved files found on the web (pinned in the manifest,
  `build/test-data/local-r13r14/` when not distributable) and the R13/R14 references. A new drawing is written in
  **family N** (AutoCAD R13/R14 itself: no owner `330` outside reactors, 162/164 header variables, no layouts, one
  paper space, VIEWPORT as R12 with MVIEW XDATA, DIMSTYLE with 270/287 and arrow block names, CLASSes without 91);
  a read file keeps its family (A2000 = AutoCAD 2002–2005 saving as R13/R14, with 330) and is written back byte for
  byte (B12). R14: LWPOLYLINE, HATCH, IMAGE native; R13: they fail as `"<TYPE> requires DXF R14 or later"`. ACIS:
  SAT 100–106 (ACIS 1.0–1.6), no SAB. Names: at most 31 characters, A–Z, digits, `$ _ -` (`$EXTNAMES` 0). What
  R13/R14 cannot hold fails before anything changes (`target_version_error()`, `src/older_checks.cpp`). Converting a
  read drawing to another version is phase 8; the typing of the records kept as read was phase 9 (1.9.0, below)
- R12 read from a file (facit F9–F22, plan `doc/plans/2026-10-25-facit-rattningar.md`): written back as R12
  from its preserved records like 2000+ — a record that did not change as read, a changed one with only
  its changed groups anew (`DxfWriter::emit_r12()`; records matched with the drawing as read by their place,
  R12's tables have no handles). EXTMIN/EXTMAX and PEXTMIN/PEXTMAX stay the file's while that space's
  geometry is unchanged; TABLE's 70 stays the file's while the table has the records it had (otherwise the
  number of records — AutoCAD's rule for an added or removed record is not established, the facit pair for
  it needs AutoCAD: `doc/facit/14_annotative.scr`, extra section). A multiline attribute of a scaled or
  rotated INSERT is made before 2018 as one ATTRIB per format run, of an unscaled one per line (the facit,
  `attributes_by_runs()`). A drawing built in memory keeps AutoCAD's R12 form
- Fonts (rättningsfas R.8, `src/fonts.cpp`): `set_font_paths()`/`font_paths()` (default
  `%WINDIR%\Fonts`) are where SHX/TrueType/OpenType fonts are found, as AutoCAD's support
  path; `TextStyle::font_family`/`font_flags` are the style's XDATA `ACAD` (AutoCAD finds
  the font by family); R12 writes a TrueType style with `txt`, as AutoCAD
- Text size (R.8 del 2, `src/text_layout.cpp`, `src/mtext_layout.cpp`): TEXT, ATTRIB, ATTDEF,
  SHAPE, MTEXT and TOLERANCE count in `$EXTMIN`/`$EXTMAX` by their box in their fonts, laid
  out as AutoCAD (MTEXT matched exactly to AutoCAD's own output); a font not found is a
  warning and simplex.shx (FONTALT), else the insertion point. `Drawing::entity_extents()`
  and `Drawing::mtext_size()` (42/43, which AutoCAD 2000/2004 do not write) give them to
  programs
- Dimension blocks (R.7, `src/dimension_block.cpp`): a DIMENSION without a block (or whose
  block is gone) gets its `*Dn` built when written, as AutoCAD builds it — matched entity for
  entity to AutoCAD's blocks in ACadSharp's 2000/2004 samples and to ODA File Converter's for
  the rest; 10/11/16/42 as built; built-in arrows as blocks of the file; points on
  `Defpoints`. DIMSTYLE has the 2000+ variables (DIMTFILL … DIMJOGANG; XDATA `ACAD_DSTYLE_*`
  in 2000/2004)
- ACIS (R.9, `src/acis.cpp`, `src/acis_extents.cpp`): 3DSOLID, REGION and BODY count in
  `$EXTMIN`/`$EXTMAX` by their B-rep, exactly — SAT from the 1/3 lines (R13–2010, each
  character but the space as 159 − c), SAB from ACDSDATA (2013+, `AcisData::sab`); edges exact,
  and the interior extremes of sphere, torus, cone (apex) and spline faces inside the face's
  loops. `AcisData` stays as read; HDxf does not create or change ACIS data (2013's SAB of a SAT
  body is made when written)
- Phase 9 foundation (step 9.0, plan `doc/plans/2026-10-27-fas-9-typer.md`, "Integrationsavtal"): every record type
  that gets typed data is one codec in `TypedRegistry` (`src/TypedRecordTypes.hpp`, `typed_record.cpp`,
  `typed_relations.cpp`, `typed_versions.cpp`; families are listed in `typed_families.cpp`): read (`RecordView`,
  `GroupCursor`: repeated subclasses, 102/101 apart, class data 1010-1071 before 1001), write (`RecordWriter`), the CLASSES entry,
  B12 lists, B19 reference descriptors, extents and a `VersionMatrix` (nine versions x two formats; generation, named error and AutoCAD status
  are separate axes). The register of every inventoried 0-name is `src/typed_registry_data.inc` (generated by
  `tools/gen_type_registry.py`, with `test/data/typed_*.tsv`); `tools/inventory_types.py` counts the corpus; `tools/fas9_facit_run.py` makes
  facit with AutoCAD 2025's core (`doc/facit/fas9_register.md` says what exists). A record a codec cannot read stays untyped, preserved, with a warning
- Phase 9 (1.9.0, steps 9.0–9.18 and 9.S): what 1.8.1 kept untyped is typed, each type a codec in `TypedRegistry`: tables
  (ACAD_TABLE, TABLESTYLE, TABLECONTENT, CELLSTYLEMAP, `*T` blocks), BLOCK/ENDBLK colours (`Block::block_color`,
  `endblk_color`), OLE and proxy (the class's data through the class's codec: `ProxyEntityData/ProxyObjectData::class_data`),
  draw order/clipping/indexes, underlays and coordination model, MATERIAL and every VISUALSTYLE form, FIELD/FIELDLIST and
  document data, evaluation graph and dynamic blocks, ACDBASSOC*, ACSH history, render/LIGHT/SUN/LIGHTLIST/SUNSTUDY, the SECTION
  family and DETAIL/SECTION view styles, NURBSURFACE and the isolines of 2000/2004 surfaces (proxy graphics), annotation
  contexts/DIMASSOC/special dimensions, AutoCAD Map, TurboCAD and ZWM. `ObjectData` is a struct deriving from `std::variant`
  (`ObjectDataVariant`): `data.get<T>()`, `get_if<T>()`, `index()`; `std::get`/`std::visit` on `Object::data` no longer work.
  Removal cascades in a drawing built in memory as well; a FIELD child ahead of others cannot be removed (named error, the code
  refers to children by position); a surface HDxf cannot draw isolines for fails in 2000/2004 naming why. Without AutoCAD
  facit a branch is "ej testat" (plan, per family; SECTION types in R13/R14 "not established"), never called verified. Checked by
  `tools/fas9_verify_batch.py` (AutoCAD 2025's core + ODA + ezdxf; 3 851 originals, 15 051 outputs); known differences from AutoCAD,
  not fixed: README ("Known differences") and the plan's 9.S outcome
- The rule for anything 2000+: follow what AutoCAD writes and Autodesk's DXF reference —
  no own workarounds (Erik, 2026-10-23)
- ACIS body builder (since 1.7.2, plan `doc/plans/2026-10-26-acis-bygg.md`, format in
  `doc/reference/hdxf_acis_sat_21500.md`): `libs/hdxf_acis/` — target `hdxf_acis` (alias `hdxf::acis`, options
  `HDXF_BUILD_ACIS` ON and `HDXF_ACIS_BUILD_TESTS`), header `hdxf_acis.hpp`, namespace `hdxf::acis`; it needs
  HDxf's public header only and the core is not changed. A program describes its exact B-rep to a
  `BodyBuilder` (typed handles, `Result<T>` with diagnostics instead of exceptions); `build_sat()` validates
  topology, orientation and geometry (edge uses, partners, loops, pcurves, numeric checks) and writes SAT 21500
  (`SatDocument`); `to_acis_data()` gives the `AcisData` of a 3DSOLID, REGION or BODY (DXF 2010 as lines, 2013 and
  2018 through HDxf's SAT→SAB). Geometry: plane, cone/cylinder, sphere, torus, B-spline surfaces
  (`exact_spl_sur`), lines, ellipses, B-spline curves (`exact_int_cur`), pcurves (`exp_par_cur`; on a
  reversed coedge the SAT's pcurve is on -t, from AutoCAD's loft); `make_box/cylinder/cone/sphere/torus` are built
  through the same model. Millimetres, SAT unit 1; no history attributes. Measured by AutoCAD's core
  (MASSPROP and AREA, `libs/hdxf_acis/tools/acad_measure.py`): 25 cases (17 built, 8 AutoCAD's own) in 2010 and 2018 equal the analytic values and
  AutoCAD's own bodies. Not verified (no facit): see section 6 of the reference. A degenerate edge is written at
  a cone's apex only; the tests are `hdxf_acis_tests`

## Code Conventions
- Namespace: `hdxf`
- Header guard: `#pragma once`
- Single public header: `include/hdxf.hpp`
- Naming: snake_case for functions/variables, PascalCase for types/classes
- No external dependencies — C++ standard library only
- No nested structs — use separate `*Types.hpp` files in `src/` when needed
- Public methods only when needed, otherwise private
- Prefer readable code over compact clever code
- Test framework is self-contained (no gtest/catch2)

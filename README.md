**English** · [Svenska](README.sv.md)

# HDxf

A C++20 library for reading, writing and editing DXF files, with no dependencies. It reads DXF R12 …
2018 and writes R12, R13, R14, 2000, 2004, 2007, 2010, 2013 and 2018, ASCII and binary, and writes them
the way AutoCAD does: a file it has read is written back byte for byte, with only what changed written
anew.

## Features

- **Every version AutoCAD writes as DXF:** read R12 … 2018 (AC1009 … AC1032); write R12, R13, R14,
  2000, 2004, 2007, 2010, 2013 and 2018, ASCII and binary.
- **Byte-for-byte round trip:** every record of a file is kept, typed or not; an unchanged file is
  written back as it was, a changed record with only its changed groups anew. AutoCAD 2025's own files
  of every version round-trip byte for byte.
- **Typed data for what AutoCAD writes:** the R12 entities, LWPOLYLINE, HATCH, SPLINE, MTEXT, MLINE,
  MULTILEADER, MESH, surfaces, 3DSOLID/REGION/BODY, tables, fields, dynamic blocks, associative
  constraints, materials, visual styles, render and lighting objects, sections, underlays, annotation
  scales and more — each with its form in each version. A record HDxf cannot read stays untyped and is
  written back unchanged, with a warning.
- **Relations follow edits:** owners, reactors, dictionaries, groups, draw order and references are
  kept consistent when records are added or removed.
- **Extents as AutoCAD computes them:** exact for splines, for text in its fonts (SHX and TrueType) and
  for ACIS solids by their B-rep.
- **UTF-8 everywhere:** every `$DWGCODEPAGE` AutoCAD uses is decoded and encoded; 2007 and later are
  UTF-8.
- **An ACIS body builder** (`hdxf::acis`): build the ACIS body of a 3DSOLID, REGION or BODY from an
  exact boundary representation.
- **Errors are never silent:** `last_error()` after every call, `warnings()` for what a load or save had
  to change.

## Reading and writing a file

```cpp
#include "hdxf.hpp"

hdxf::Drawing dwg;
if (!dwg.load("plan.dxf"))
    return 1;                                   // dwg.last_error() says why

for (const hdxf::Entity& e : dwg.entities())
    if (e.type == hdxf::EntityType::Line) {
        const hdxf::LineData& line = e.as_line();
        // line.start, line.end
    }

dwg.add_circle({50, 25, 0}, 20.0);
dwg.save_as("plan_edited.dxf");                 // the file's own version and format

// A new drawing in a chosen version
hdxf::Drawing out(hdxf::DxfVersion::R2018);
out.add_layer("Walls", hdxf::Color::red());
out.add_line({0, 0, 0}, {100, 50, 0}, "Walls");
out.save_as("new.dxf", hdxf::DxfFormat::Binary);
```

## Using HDxf in your project

```cmake
include(FetchContent)
FetchContent_Declare(hdxf
    GIT_REPOSITORY https://github.com/holmbom/HDxf.git
    GIT_TAG        v1.9.1)   # or another release tag
FetchContent_MakeAvailable(hdxf)

target_link_libraries(myapp PRIVATE hdxf::hdxf)   # hdxf::acis for the ACIS body builder
```

Only the library is built in your project; the tests and tools are not.

### Prebuilt libraries

Every release has prebuilt Release libraries for Windows (MSVC and MinGW) and Linux (GCC), so the
library does not have to be compiled in your project. Unpack one and point CMake at it:

```cmake
find_package(hdxf 1 CONFIG REQUIRED)   # -DCMAKE_PREFIX_PATH=path/to/HDxf-vX.Y.Z-…
target_link_libraries(myapp PRIVATE hdxf::hdxf)
```

Pick the package for your compiler: static C++ libraries cannot be mixed between MSVC, MinGW and GCC.
All packages are Release builds; a Debug build of your app on MSVC needs the source instead.

## Building

Requirements: CMake 3.20+, Ninja and a C++20 compiler (GCC, MSVC or Clang). Windows and Linux; macOS is
not yet tested.

```sh
cmake -B build -G Ninja
cmake --build build
python -I tools/run_tests.py
```

- Without a build type the build is Release.
- `tools/run_tests.py` runs the test suite in parallel shards; `./build/test/hdxf_tests [filter …]` runs
  tests in one process.
- `-DHDXF_BUILD_TESTS=OFF` skips the tests; `-DHDXF_BUILD_ACIS=OFF` leaves out the ACIS body builder.
- `-DHDXF_TEST_DATA=OFF` does not download the external test files. They are not in the repository;
  [test/data/manifest.cmake](test/data/manifest.cmake) lists them, and tests whose file is missing print
  `SKIP`.

## Known issues

- **MSVC:** one test fails — `dxf_unicode.dxf` in the material and visual style corpus is not written back as it was
  read when its records are written from their typed values. GCC (MinGW and Linux) passes. To be fixed in 1.9.2.

## Documentation

- [README.sv.md](README.sv.md): the full description, with every entity, version and API call. In
  Swedish.
- [CLAUDE.md](CLAUDE.md): the developer guide: architecture, the rules for each DXF version and the
  design decisions. Written for both people and AI coding tools.
- [TODO.md](TODO.md) and [doc/plans/](doc/plans/): what is done and what is left, phase by phase. In
  Swedish.

## License

[MIT](LICENSE) © 2026 Erik Holmbom

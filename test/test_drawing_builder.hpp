#pragma once

#include "hdxf.hpp"

// Builds a drawing that exercises all R12 features. Shared by generate_test_dxf
// and the tests.
//
// Levels:
//   1 = LINE, CIRCLE, ARC, POINT, TEXT (basics)
//   2 = + POLYLINE (with bulge)
//   3 = + TRACE, SOLID, 3DFACE
//   4 = + blocks + INSERT (plain)
//   5 = + INSERT with attributes + ATTDEF
//   6 = + DIMENSION (all 7 subtypes)
//   7 = + VIEWPORT (paper space)
hdxf::Drawing build_test_drawing(int level = 7);

// The level 7 drawing written as R12 (ASCII) and read back: a drawing as read from an R12 file
hdxf::Drawing r12_drawing_read_back();

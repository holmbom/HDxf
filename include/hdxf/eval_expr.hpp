#pragma once

// The evaluation values that dynamic block elements and solid history nodes share.

#include "core.hpp"

namespace hdxf {

// ---------------------------------------------------------------------------
// Value types the typed records of phase 9 share (plan 2026-10-27-fas-9-typer.md, step 9.0)
// ---------------------------------------------------------------------------

// AcDbEvalVariant: the value of an evaluation expression and of an associative value parameter,
// as AutoCAD writes it — a type tag (70) followed by the value in the group that tag names. AutoCAD's
// files have two tags: 40 (a double, in 140 in AcDbEvalExpr) and 70 (an int16, in a second 70). A record
// with another tag is not typed (no AutoCAD file has one): it stays as it was read, with a warning.
struct EvalVariant {
    int tag = 40;                       // 70: 40 real, 70 int16 (the tags AutoCAD's files have)
    double real = 0.0;                  // tag 40
    int32_t integer = 0;                // tag 70
};

// The value part of AcDbEvalExpr that is present only on some nodes: the expression text (1) and its
// current value
struct EvalExprValue {
    std::string expression;             // 1
    EvalVariant value;                  // 70 and its value group
};

// AcDbEvalExpr, the first subclass of every dynamic block element and of the solid history nodes: the
// graph node's id (an integer in the ACAD_EVALUATION_GRAPH, not a handle), the class version and,
// on some nodes, an expression with its value
struct EvalExprData {
    int32_t node_id = 0;                // 90
    int32_t version = 0;                // 98
    int32_t maintenance = 0;            // 99
    std::optional<EvalExprValue> expression;    // 1, 70, value: present on some nodes only
};

} // namespace hdxf

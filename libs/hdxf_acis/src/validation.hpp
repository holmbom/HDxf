#pragma once

// The checks a body passes before it is written (internal)

#include "model.hpp"

#include <string>
#include <vector>

namespace hdxf::acis::detail {

// Why a transform is not one the SAT can hold (rotation, positive uniform scale); empty when it is
std::string check_transform(const Transform& transform, const Tolerances& tolerance);

// Whether the edge's curve is traversed in increasing parameter by a coedge: the coedge runs
// from the edge's start to its end unless reversed, and the start is at the lower parameter
// unless the edge runs against its curve
bool coedge_increasing(const CoedgeRecord& coedge, const EdgeRecord& edge);

// Everything wrong with the body, structure first. Empty: the body can be written.
std::vector<Diagnostic> validate_body(const BuilderData& data, BodyId body);

} // namespace hdxf::acis::detail

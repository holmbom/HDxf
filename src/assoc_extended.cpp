#include "AssocExtended.hpp"

// Step 9.15's registration: the other ACDBASSOC classes (plan 2026-10-27-fas-9-typer.md). typed_families.cpp calls this one
// function; the codecs are in assoc_constraints.cpp, assoc_extended_params.cpp, assoc_surface_ops.cpp and
// assoc_dimensions_arrays.cpp.

namespace hdxf {
namespace internal {

void register_assoc_extended_codecs(TypedRegistry& registry) {
    register_assoc_constraint_codecs(registry);
    register_assoc_extended_param_codecs(registry);
    register_assoc_surface_op_codecs(registry);
    register_assoc_dimension_array_codecs(registry);
}

} // namespace internal
} // namespace hdxf

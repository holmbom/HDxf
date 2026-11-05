#include "hdxf/entity.hpp"
#include "hdxf/entity_light_underlay.hpp"
#include "hdxf/entity_mesh.hpp"
#include "hdxf/entity_mleader.hpp"
#include "hdxf/entity_special.hpp"
#include "hdxf/entity_surface.hpp"
#include "hdxf/entity_viewport_image.hpp"
#include "hdxf/table.hpp"
#include <array>
#include <cstddef>
#include <utility>

// EntityData's copying, moving, destruction, construction and access (see entity.hpp): made here only, optimized and
// without debug information (src/CMakeLists.txt), so that libstdc++'s nested helpers are not each written out with
// its long name
namespace hdxf {

namespace {

using EmplaceMoved = void (*)(EntityDataVariant&, void*);

template <std::size_t I>
void emplace_moved_at(EntityDataVariant& data, void* value) {
    using T = std::variant_alternative_t<I, EntityDataVariant>;
    data.template emplace<I>(std::move(*static_cast<T*>(value)));
}

template <std::size_t... I>
constexpr auto emplace_moved_table(std::index_sequence<I...>) {
    return std::array<EmplaceMoved, sizeof...(I)>{&emplace_moved_at<I>...};
}

constexpr auto emplace_moved_by_index =
    emplace_moved_table(std::make_index_sequence<std::variant_size_v<EntityDataVariant>>{});

} // namespace

static_assert(std::is_nothrow_move_constructible_v<EntityDataVariant>);
static_assert(std::is_nothrow_move_assignable_v<EntityDataVariant>);

EntityData::EntityData() = default;
EntityData::EntityData(const EntityData& other) = default;
EntityData::EntityData(EntityData&& other) noexcept = default;
EntityData& EntityData::operator=(const EntityData& other) = default;
EntityData& EntityData::operator=(EntityData&& other) noexcept = default;
EntityData::~EntityData() = default;

std::size_t EntityData::index() const noexcept {
    return EntityDataVariant::index();
}

bool EntityData::valueless_by_exception() const noexcept {
    return EntityDataVariant::valueless_by_exception();
}

const void* EntityData::held() const noexcept {
    if (EntityDataVariant::valueless_by_exception()) return nullptr;
    return std::visit([](const auto& value) -> const void* { return &value; }, static_cast<const EntityDataVariant&>(*this));
}

EntityData::EntityData(std::size_t index, void* value) {
    emplace_moved_by_index[index](*this, value);
}

const DimensionData* Entity::dimension_part() const {
    if (auto* d = data.get_if<DimensionData>()) return d;
    if (auto* a = data.get_if<Boxed<ArcDimensionData>>()) return &(**a).dimension;
    if (auto* l = data.get_if<Boxed<LargeRadialDimensionData>>()) return &(**l).dimension;
    return nullptr;
}

} // namespace hdxf

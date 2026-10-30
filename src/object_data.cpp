#include "hdxf.hpp"
#include <cstddef>
#include <utility>

// ObjectData's copying, moving, destruction, construction and access (see hdxf.hpp): made here only, optimized and
// without debug information (src/CMakeLists.txt), so that libstdc++'s nested helpers are not each written out with
// its long name
namespace hdxf {

namespace {

using EmplaceMoved = void (*)(ObjectDataVariant&, void*);

template <std::size_t I>
void emplace_moved_at(ObjectDataVariant& data, void* value) {
    using T = std::variant_alternative_t<I, ObjectDataVariant>;
    data.template emplace<I>(std::move(*static_cast<T*>(value)));
}

template <std::size_t... I>
constexpr auto emplace_moved_table(std::index_sequence<I...>) {
    return std::array<EmplaceMoved, sizeof...(I)>{&emplace_moved_at<I>...};
}

constexpr auto emplace_moved_by_index =
    emplace_moved_table(std::make_index_sequence<std::variant_size_v<ObjectDataVariant>>{});

} // namespace

ObjectData::ObjectData(const ObjectData& other) = default;
ObjectData::ObjectData(ObjectData&& other) noexcept = default;
ObjectData& ObjectData::operator=(const ObjectData& other) = default;
ObjectData& ObjectData::operator=(ObjectData&& other) noexcept = default;
ObjectData::~ObjectData() = default;

const void* ObjectData::held() const noexcept {
    if (valueless_by_exception()) return nullptr;
    return std::visit([](const auto& value) -> const void* { return &value; }, static_cast<const ObjectDataVariant&>(*this));
}

ObjectData::ObjectData(std::size_t index, void* value) {
    emplace_moved_by_index[index](*this, value);
}

}  // namespace hdxf

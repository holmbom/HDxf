#pragma once

#include "hdxf.hpp"
#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

// A visit over ObjectData's alternatives that libstdc++ does not make for each file again: std::visit on the some
// 200 alternatives writes out its nested helpers, each with a name of the whole list (hundreds of MB per file). Here the
// held alternative's address comes from object_data.cpp (ObjectData::held()), and each alternative costs one small
// function.
namespace hdxf::internal {

template <typename Data, typename F, std::size_t I>
decltype(auto) visit_object_alternative(F& f, const void* held) {
    using T = std::variant_alternative_t<I, ObjectDataVariant>;
    if constexpr (std::is_const_v<Data>) {
        return f(*static_cast<const T*>(held));
    } else {
        return f(*static_cast<T*>(const_cast<void*>(held)));
    }
}

template <typename Data, typename F, std::size_t... I>
decltype(auto) visit_object_data(F& f, Data& data, std::index_sequence<I...>) {
    using R = decltype(visit_object_alternative<Data, F, 0>(f, nullptr));
    static constexpr R (*table[])(F&, const void*) = {&visit_object_alternative<Data, F, I>...};
    if (data.valueless_by_exception()) throw std::bad_variant_access();     // as std::visit
    return table[data.index()](f, data.held());
}

// std::visit(f, data) for an Object's data; every alternative must give f's result the same type
template <typename F, typename Data>
    requires std::is_same_v<std::remove_const_t<Data>, ObjectData>
decltype(auto) visit_object_data(F f, Data& data) {
    return visit_object_data(f, data, std::make_index_sequence<std::variant_size_v<ObjectDataVariant>>{});
}

} // namespace hdxf::internal

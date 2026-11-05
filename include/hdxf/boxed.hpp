#pragma once

// Values kept on the heap, and what ObjectData and EntityData share. Boxed<T> and OptionalBoxed<T> hold a pointer, so T may
// be a declared type for as long as nothing copies, makes or destroys one.

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

namespace hdxf {

// A value on the heap that may be absent, copied deeply — for the parts of an entity that few
// have (MTEXT's columns, a multiline attribute's text), so they do not make every Entity
// larger (plan phase 6, B84). Converts from T, so `data.columns = MTextColumns{…}` works.
template <typename T>
class OptionalBoxed {
public:
    OptionalBoxed() = default;
    OptionalBoxed(T value) : p_(std::make_unique<T>(std::move(value))) {}
    OptionalBoxed(const OptionalBoxed& other) : p_(other.p_ ? std::make_unique<T>(*other.p_) : nullptr) {}
    OptionalBoxed(OptionalBoxed&& other) noexcept = default;
    OptionalBoxed& operator=(const OptionalBoxed& other) {
        if (this != &other) p_ = other.p_ ? std::make_unique<T>(*other.p_) : nullptr;
        return *this;
    }
    OptionalBoxed& operator=(OptionalBoxed&& other) noexcept = default;
    OptionalBoxed& operator=(T value) {
        p_ = std::make_unique<T>(std::move(value));
        return *this;
    }
    bool has_value() const { return p_ != nullptr; }
    explicit operator bool() const { return p_ != nullptr; }
    T& operator*() { return *p_; }
    const T& operator*() const { return *p_; }
    T* operator->() { return p_.get(); }
    const T* operator->() const { return p_.get(); }
    T& emplace() {
        p_ = std::make_unique<T>();
        return *p_;
    }
    void reset() { p_.reset(); }

private:
    std::unique_ptr<T> p_;
};

// A value kept on the heap and copied deeply — for entity data that is large but rare
// (VIEWPORT), so it does not make every Entity larger. Converts from T, so
// `entity.data = ViewportData{…}` works; Entity::as_viewport() gives the T.
template <typename T>
class Boxed {
public:
    Boxed() : p_(std::make_unique<T>()) {}
    Boxed(T value) : p_(std::make_unique<T>(std::move(value))) {}
    Boxed(const Boxed& other) : p_(std::make_unique<T>(*other.p_)) {}
    Boxed(Boxed&& other) noexcept : p_(std::move(other.p_)) {}
    Boxed& operator=(const Boxed& other) {
        if (this != &other) p_ = std::make_unique<T>(*other.p_);
        return *this;
    }
    Boxed& operator=(Boxed&& other) noexcept {
        p_ = std::move(other.p_);
        return *this;
    }
    T& operator*() { return *p_; }
    const T& operator*() const { return *p_; }

private:
    std::unique_ptr<T> p_;
};

// A variant's alternative for a value of type T: the one std::variant's converting constructor takes (SomeData's is
// Boxed<SomeData> when only that is an alternative)
template <typename T>
struct DataPick {
    static T pick(T);
};

template <typename V>
struct DataPicks;

template <typename... Ts>
struct DataPicks<std::variant<Ts...>> : DataPick<Ts>... {
    using DataPick<Ts>::pick...;
};

template <typename V, typename T>
using DataPicked = decltype(DataPicks<V>::pick(std::declval<T>()));

// std::in_place_type / std::in_place_index, which ObjectData and EntityData take as std::variant does
template <typename T>
struct DataInPlace : std::false_type {};
template <typename T>
struct DataInPlace<std::in_place_type_t<T>> : std::true_type {};
template <std::size_t I>
struct DataInPlace<std::in_place_index_t<I>> : std::true_type {};

// The index of the alternative T
template <typename T, typename... Ts>
consteval std::size_t data_index_in(const std::variant<Ts...>*) {
    constexpr bool same[] = {std::is_same_v<T, Ts>...};
    for (std::size_t i = 0; i < sizeof...(Ts); ++i) {
        if (same[i]) return i;
    }
    return sizeof...(Ts);
}

} // namespace hdxf

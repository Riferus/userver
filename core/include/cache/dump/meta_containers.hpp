#pragma once

#include <iterator>
#include <map>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <cache/dump/meta.hpp>

namespace cache::dump {

/// @{
/// Customization point: insert an element into a container
template <typename T, typename Alloc>
void Insert(std::vector<T, Alloc>& cont, T&& elem) {
  cont.push_back(std::forward<T>(elem));
}

template <typename K, typename V, typename Comp, typename Alloc>
void Insert(std::map<K, V, Comp, Alloc>& cont, std::pair<const K, V>&& elem) {
  cont.insert(std::move(elem));
}

template <typename K, typename V, typename Hash, typename Eq, typename Alloc>
void Insert(std::unordered_map<K, V, Hash, Eq, Alloc>& cont,
            std::pair<const K, V>&& elem) {
  cont.insert(std::move(elem));
}

template <typename T, typename Comp, typename Alloc>
void Insert(std::set<T, Comp, Alloc>& cont, T&& elem) {
  cont.insert(std::forward<T>(elem));
}

template <typename T, typename Hash, typename Eq, typename Alloc>
void Insert(std::unordered_set<T, Hash, Eq, Alloc>& cont, T&& elem) {
  cont.insert(std::forward<T>(elem));
}
/// @}

namespace impl {

template <typename T>
using RangeSizeResult = decltype(std::size(std::declval<const T&>()));

template <typename T>
inline constexpr bool kIsSizeable =
    std::is_same_v<meta::DetectedType<RangeSizeResult, T>, std::size_t>;

template <typename T>
using InsertResult = decltype(cache::dump::Insert(
    std::declval<T&>(), std::declval<meta::RangeValueType<T>&&>()));

template <typename T>
using ReserveResult = decltype(std::declval<T&>().reserve(1));

}  // namespace impl

/// Check if a range is a container
template <typename T>
inline constexpr bool kIsContainer =
    meta::kIsRange<T>&& std::is_default_constructible_v<T>&&
        impl::kIsSizeable<T>&& meta::kIsDetected<impl::InsertResult, T>;

/// Check if a container has `reserve`
template <typename T>
inline constexpr bool kIsReservable = meta::kIsDetected<impl::ReserveResult, T>;

}  // namespace cache::dump

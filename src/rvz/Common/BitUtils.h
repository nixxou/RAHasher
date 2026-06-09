// Shim for Dolphin's Common/BitUtils.h — only the AsU8Span helpers used by the RVZ reader.
#pragma once
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>

#include "Common/CommonTypes.h"

namespace Common
{
// Convert a contiguous range of a trivially-copyable type to a span<const u8>
template <std::ranges::contiguous_range T>
  requires(std::is_trivially_copyable_v<std::ranges::range_value_t<T>>)
constexpr auto AsU8Span(const T& range)
{
  return std::span{reinterpret_cast<const u8*>(range.data()), range.size() * sizeof(*range.data())};
}

// Convert a contiguous range of a non-const trivially-copyable type to a span<u8>
template <std::ranges::contiguous_range T>
  requires(std::is_trivially_copyable_v<std::ranges::range_value_t<T>>)
constexpr auto AsWritableU8Span(T& range)
{
  return std::span{reinterpret_cast<u8*>(range.data()), range.size() * sizeof(*range.data())};
}

// Convert a trivially-copyable object to a span<const u8>
template <typename T>
  requires(!std::ranges::contiguous_range<T> && std::is_trivially_copyable_v<T>)
constexpr auto AsU8Span(const T& obj)
{
  return std::span{reinterpret_cast<const u8*>(std::addressof(obj)), sizeof(obj)};
}

// Convert a non-const trivially-copyable object to a span<u8>
template <typename T>
  requires(!std::ranges::contiguous_range<T> && std::is_trivially_copyable_v<T>)
constexpr auto AsWritableU8Span(T& obj)
{
  return std::span{reinterpret_cast<u8*>(std::addressof(obj)), sizeof(obj)};
}
}  // namespace Common

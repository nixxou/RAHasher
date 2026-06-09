// Shim for Dolphin's Common/Align.h
#pragma once
#include <cstddef>
#include <type_traits>

namespace Common
{
template <typename T>
constexpr T AlignUp(T value, size_t size)
{
  static_assert(std::is_integral_v<T>);
  return static_cast<T>(value + (size - static_cast<size_t>(value) % size) % size);
}

template <typename T>
constexpr T AlignDown(T value, size_t size)
{
  static_assert(std::is_integral_v<T>);
  return static_cast<T>(value - static_cast<size_t>(value) % size);
}
}  // namespace Common

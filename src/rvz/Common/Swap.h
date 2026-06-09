// Shim for Dolphin's Common/Swap.h — byteswap helpers used by the RVZ reader.
#pragma once
#include <cstdlib>
#include <cstring>
#include <type_traits>

#include "Common/CommonTypes.h"

namespace Common
{
inline u8 swap8(u8 data) { return data; }
inline u32 swap24(const u8* data) { return (data[0] << 16) | (data[1] << 8) | data[2]; }

#ifdef _WIN32
inline u16 swap16(u16 data) { return _byteswap_ushort(data); }
inline u32 swap32(u32 data) { return _byteswap_ulong(data); }
inline u64 swap64(u64 data) { return _byteswap_uint64(data); }
#else
inline u16 swap16(u16 data) { return static_cast<u16>((data >> 8) | (data << 8)); }
inline u32 swap32(u32 data) { return (static_cast<u32>(swap16(static_cast<u16>(data))) << 16) | swap16(static_cast<u16>(data >> 16)); }
inline u64 swap64(u64 data) { return (static_cast<u64>(swap32(static_cast<u32>(data))) << 32) | swap32(static_cast<u32>(data >> 32)); }
#endif

inline u16 swap16(const u8* data) { u16 v; std::memcpy(&v, data, sizeof(v)); return swap16(v); }
inline u32 swap32(const u8* data) { u32 v; std::memcpy(&v, data, sizeof(v)); return swap32(v); }
inline u64 swap64(const u8* data) { u64 v; std::memcpy(&v, data, sizeof(v)); return swap64(v); }

template <typename T>
inline T FromBigEndian(T value)
{
  static_assert(std::is_integral_v<T>);
  if constexpr (sizeof(T) == 1)
    return value;
  else if constexpr (sizeof(T) == 2)
    return static_cast<T>(swap16(static_cast<u16>(value)));
  else if constexpr (sizeof(T) == 4)
    return static_cast<T>(swap32(static_cast<u32>(value)));
  else
    return static_cast<T>(swap64(static_cast<u64>(value)));
}
}  // namespace Common

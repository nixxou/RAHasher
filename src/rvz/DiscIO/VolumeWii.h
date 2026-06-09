// Shim for Dolphin's DiscIO/VolumeWii.h — only the block/group constants and HashBlock layout
// referenced by the RVZ reader. (Wii decryption itself is out of scope for the GameCube phase.)
#pragma once
#include "Common/CommonTypes.h"

namespace DiscIO
{
class VolumeWii
{
public:
  static constexpr u64 BLOCK_HEADER_SIZE = 0x0400;
  static constexpr u64 BLOCK_DATA_SIZE = 0x7C00;
  static constexpr u64 BLOCK_TOTAL_SIZE = BLOCK_HEADER_SIZE + BLOCK_DATA_SIZE;  // 0x8000
  static constexpr u64 BLOCKS_PER_GROUP = 0x40;
  static constexpr u64 GROUP_HEADER_SIZE = BLOCK_HEADER_SIZE * BLOCKS_PER_GROUP;  // 0x10000
  static constexpr u64 GROUP_DATA_SIZE = BLOCK_DATA_SIZE * BLOCKS_PER_GROUP;      // 0x1F0000
  static constexpr u64 GROUP_TOTAL_SIZE = BLOCK_TOTAL_SIZE * BLOCKS_PER_GROUP;    // 0x200000

#pragma pack(push, 1)
  struct HashBlock
  {
    u8 h0[31][20];
    u8 padding_0[20];
    u8 h1[8][20];
    u8 padding_1[32];
    u8 h2[8][20];
    u8 padding_2[32];
  };
#pragma pack(pop)
  static_assert(sizeof(HashBlock) == BLOCK_HEADER_SIZE, "Wrong size for Wii hash block");
};
}  // namespace DiscIO

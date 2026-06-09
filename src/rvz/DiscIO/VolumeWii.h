// Shim for Dolphin's DiscIO/VolumeWii.h — the block/group constants, the HashBlock layout, and
// the two static helpers the RVZ Wii re-encryption path needs (HashGroup / EncryptGroup).
// The full VolumeWii (volume parsing, decryption, tickets...) is NOT ported.
#pragma once
#include <array>
#include <functional>

#include "Common/CommonTypes.h"
#include "Common/Crypto/AES.h"
#include "Common/Crypto/SHA1.h"

namespace DiscIO
{
class BlobReader;

class VolumeWii
{
public:
  static constexpr size_t AES_KEY_SIZE = Common::AES::Context::KEY_SIZE;  // 16

  static constexpr u32 BLOCKS_PER_GROUP = 0x40;

  static constexpr u64 BLOCK_HEADER_SIZE = 0x0400;
  static constexpr u64 BLOCK_DATA_SIZE = 0x7C00;
  static constexpr u64 BLOCK_TOTAL_SIZE = BLOCK_HEADER_SIZE + BLOCK_DATA_SIZE;  // 0x8000

  static constexpr u64 GROUP_HEADER_SIZE = BLOCK_HEADER_SIZE * BLOCKS_PER_GROUP;  // 0x10000
  static constexpr u64 GROUP_DATA_SIZE = BLOCK_DATA_SIZE * BLOCKS_PER_GROUP;      // 0x1F0000
  static constexpr u64 GROUP_TOTAL_SIZE = GROUP_HEADER_SIZE + GROUP_DATA_SIZE;    // 0x200000

  struct HashBlock
  {
    std::array<Common::SHA1::Digest, 31> h0;
    std::array<u8, 20> padding_0;
    std::array<Common::SHA1::Digest, 8> h1;
    std::array<u8, 32> padding_1;
    std::array<Common::SHA1::Digest, 8> h2;
    std::array<u8, 32> padding_2;
  };
  static_assert(sizeof(HashBlock) == BLOCK_HEADER_SIZE, "Wrong size for Wii hash block");

  // Computes the H0/H1/H2 hashes for a group of BLOCKS_PER_GROUP decrypted data blocks.
  // read_function (if set) is called to populate in[block] just before it is hashed.
  static bool HashGroup(const std::array<u8, BLOCK_DATA_SIZE> in[BLOCKS_PER_GROUP],
                        HashBlock out[BLOCKS_PER_GROUP],
                        const std::function<bool(size_t block)>& read_function = {});

  // Reconstructs one encrypted group (GROUP_TOTAL_SIZE bytes): reads the decrypted data via
  // blob->ReadWiiDecrypted, hashes it, optionally fixes up hashes via hash_exception_callback,
  // then AES-128-CBC encrypts each block (hash header IV=0, data IV = encrypted bytes at 0x3D0).
  static bool EncryptGroup(u64 offset, u64 partition_data_offset, u64 partition_data_decrypted_size,
                           const std::array<u8, AES_KEY_SIZE>& key, BlobReader* blob,
                           std::array<u8, GROUP_TOTAL_SIZE>* out,
                           const std::function<void(HashBlock hash_blocks[BLOCKS_PER_GROUP])>&
                               hash_exception_callback = {});
};
}  // namespace DiscIO

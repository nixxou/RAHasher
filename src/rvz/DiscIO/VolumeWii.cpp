// Standalone port of Dolphin's DiscIO::VolumeWii::HashGroup and DiscIO::VolumeWii::EncryptGroup.
//
// Only these two static helpers are ported. The result computed in out[] is byte-for-byte
// identical to Dolphin's, but the multithreading (std::async/std::future/std::thread) has been
// removed in favor of plain sequential loops. The dependency ordering of the original is
// preserved: a block's H1 slot requires its own H0; a sub-group's H2 slot requires its 8 H1s.

#include "DiscIO/VolumeWii.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <vector>

#include "Common/Align.h"
#include "Common/CommonTypes.h"
#include "Common/Crypto/AES.h"
#include "Common/Crypto/SHA1.h"
#include "DiscIO/Blob.h"

namespace DiscIO
{
bool VolumeWii::HashGroup(const std::array<u8, BLOCK_DATA_SIZE> in[BLOCKS_PER_GROUP],
                          HashBlock out[BLOCKS_PER_GROUP],
                          const std::function<bool(size_t block)>& read_function)
{
  bool success = true;

  for (size_t i = 0; i < BLOCKS_PER_GROUP; ++i)
  {
    if (read_function && success)
      success = read_function(i);

    const size_t h1_base = Common::AlignDown(i, 8);

    if (success)
    {
      // H0 hashes
      for (size_t j = 0; j < 31; ++j)
        out[i].h0[j] = Common::SHA1::CalculateDigest(in[i].data() + j * 0x400, 0x400);

      // H0 padding
      out[i].padding_0 = {};

      // H1 hash
      out[h1_base].h1[i - h1_base] = Common::SHA1::CalculateDigest(out[i].h0);
    }

    if (i % 8 == 7)
    {
      // In Dolphin this point is reached only after the 8 blocks of the sub-group have been
      // hashed; sequentially that is already the case here.
      if (success)
      {
        // H1 padding
        out[h1_base].padding_1 = {};

        // H1 copies
        for (size_t j = 1; j < 8; ++j)
          out[h1_base + j].h1 = out[h1_base].h1;

        // H2 hash
        out[0].h2[h1_base / 8] = Common::SHA1::CalculateDigest(out[i].h1);
      }

      if (i == BLOCKS_PER_GROUP - 1)
      {
        // All sub-groups (and thus all H2 slots) have been computed by now.
        if (success)
        {
          // H2 padding
          out[0].padding_2 = {};

          // H2 copies
          for (size_t j = 1; j < BLOCKS_PER_GROUP; ++j)
            out[j].h2 = out[0].h2;
        }
      }
    }
  }

  return success;
}

bool VolumeWii::EncryptGroup(
    u64 offset, u64 partition_data_offset, u64 partition_data_decrypted_size,
    const std::array<u8, AES_KEY_SIZE>& key, BlobReader* blob,
    std::array<u8, GROUP_TOTAL_SIZE>* out,
    const std::function<void(HashBlock hash_blocks[BLOCKS_PER_GROUP])>& hash_exception_callback)
{
  std::vector<std::array<u8, BLOCK_DATA_SIZE>> unencrypted_data(BLOCKS_PER_GROUP);
  std::vector<HashBlock> unencrypted_hashes(BLOCKS_PER_GROUP);

  const bool success =
      HashGroup(unencrypted_data.data(), unencrypted_hashes.data(), [&](size_t block) {
        if (offset + (block + 1) * BLOCK_DATA_SIZE <= partition_data_decrypted_size)
        {
          if (!blob->ReadWiiDecrypted(offset + block * BLOCK_DATA_SIZE, BLOCK_DATA_SIZE,
                                      unencrypted_data[block].data(), partition_data_offset))
          {
            return false;
          }
        }
        else
        {
          unencrypted_data[block].fill(0);
        }
        return true;
      });

  if (!success)
    return false;

  if (hash_exception_callback)
    hash_exception_callback(unencrypted_hashes.data());

  auto aes_context = Common::AES::CreateContextEncrypt(key.data());

  for (size_t j = 0; j < BLOCKS_PER_GROUP; ++j)
  {
    u8* out_ptr = out->data() + j * BLOCK_TOTAL_SIZE;

    aes_context->CryptIvZero(reinterpret_cast<const u8*>(&unencrypted_hashes[j]), out_ptr,
                             BLOCK_HEADER_SIZE);

    aes_context->Crypt(out_ptr + 0x3D0, unencrypted_data[j].data(), out_ptr + BLOCK_HEADER_SIZE,
                       BLOCK_DATA_SIZE);
  }

  return true;
}
}  // namespace DiscIO

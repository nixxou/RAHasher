// Copyright 2020 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Reduced port for the standalone RVZ hashing harness: decompression only.
// Bzip2/LZMA are kept as stubs (this build ships only zstd), and all Compressor
// (write-side) classes have been removed.

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include <zstd.h>

#include "Common/CommonTypes.h"
#include "Common/Crypto/SHA1.h"
#include "DiscIO/LaggedFibonacciGenerator.h"

namespace DiscIO
{
struct DecompressionBuffer
{
  std::vector<u8> data;
  size_t bytes_written = 0;
};

struct PurgeSegment
{
  u32 offset;
  u32 size;
};
static_assert(sizeof(PurgeSegment) == 0x08, "Wrong size for WIA purge segment");

class Decompressor
{
public:
  virtual ~Decompressor();

  virtual bool Decompress(const DecompressionBuffer& in, DecompressionBuffer* out,
                          size_t* in_bytes_read) = 0;
  virtual bool Done() const { return m_done; }

protected:
  bool m_done = false;
};

class NoneDecompressor final : public Decompressor
{
public:
  bool Decompress(const DecompressionBuffer& in, DecompressionBuffer* out,
                  size_t* in_bytes_read) override;
};

// This class assumes that more bytes won't be added to in once in.bytes_written == in.data.size()
// and that *in_bytes_read initially will be equal to the size of the exception lists
class PurgeDecompressor final : public Decompressor
{
public:
  PurgeDecompressor(u64 decompressed_size);
  bool Decompress(const DecompressionBuffer& in, DecompressionBuffer* out,
                  size_t* in_bytes_read) override;

private:
  const u64 m_decompressed_size;

  PurgeSegment m_segment = {};
  size_t m_bytes_read = 0;
  size_t m_segment_bytes_written = 0;
  size_t m_out_bytes_written = 0;
  bool m_started = false;

  std::unique_ptr<Common::SHA1::Context> m_sha1_context;
};

// NOT SUPPORTED in this standalone build (no libbz2). Stub: Decompress() always fails.
// Kept only so the compression_type switch in WIABlob still compiles.
class Bzip2Decompressor final : public Decompressor
{
public:
  bool Decompress(const DecompressionBuffer& in, DecompressionBuffer* out,
                  size_t* in_bytes_read) override;
};

// NOT SUPPORTED in this standalone build (bundled lzma is the 7-Zip SDK, not xz/liblzma).
// Stub: Decompress() always fails. Constructor signature kept to match the WIABlob switch.
class LZMADecompressor final : public Decompressor
{
public:
  LZMADecompressor(bool lzma2, const u8* filter_options, size_t filter_options_size);
  bool Decompress(const DecompressionBuffer& in, DecompressionBuffer* out,
                  size_t* in_bytes_read) override;
};

class ZstdDecompressor final : public Decompressor
{
public:
  ZstdDecompressor();
  ~ZstdDecompressor() override;

  bool Decompress(const DecompressionBuffer& in, DecompressionBuffer* out,
                  size_t* in_bytes_read) override;

private:
  ZSTD_DStream* m_stream;
};

class RVZPackDecompressor final : public Decompressor
{
public:
  RVZPackDecompressor(std::unique_ptr<Decompressor> decompressor, DecompressionBuffer decompressed,
                      u64 data_offset, u32 rvz_packed_size);

  bool Decompress(const DecompressionBuffer& in, DecompressionBuffer* out,
                  size_t* in_bytes_read) override;

  bool Done() const override;

private:
  bool IncrementBytesRead(size_t x);
  std::optional<bool> ReadToDecompressed(const DecompressionBuffer& in, size_t* in_bytes_read,
                                         size_t decompressed_bytes_read, size_t bytes_to_read);

  std::unique_ptr<Decompressor> m_decompressor;
  DecompressionBuffer m_decompressed;
  size_t m_decompressed_bytes_read = 0;
  size_t m_bytes_read;
  u64 m_data_offset;
  u32 m_rvz_packed_size;

  u32 m_size = 0;
  bool m_junk = false;
  LaggedFibonacciGenerator m_lfg;
};

}  // namespace DiscIO

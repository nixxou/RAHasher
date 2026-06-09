// Copyright 2020 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Reduced port for the standalone RVZ hashing harness: decompression only.
// - Bzip2Decompressor / LZMADecompressor are intentional stubs (see below).
// - All Compressor (write-side) classes have been removed (out of scope).

#include "DiscIO/WIACompression.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <zstd.h>

#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/Swap.h"
#include "DiscIO/LaggedFibonacciGenerator.h"

namespace DiscIO
{
Decompressor::~Decompressor() = default;

bool NoneDecompressor::Decompress(const DecompressionBuffer& in, DecompressionBuffer* out,
                                  size_t* in_bytes_read)
{
  const size_t length =
      std::min(in.bytes_written - *in_bytes_read, out->data.size() - out->bytes_written);

  std::memcpy(out->data.data() + out->bytes_written, in.data.data() + *in_bytes_read, length);

  *in_bytes_read += length;
  out->bytes_written += length;

  m_done = in.data.size() == *in_bytes_read;
  return true;
}

PurgeDecompressor::PurgeDecompressor(u64 decompressed_size) : m_decompressed_size(decompressed_size)
{
}

bool PurgeDecompressor::Decompress(const DecompressionBuffer& in, DecompressionBuffer* out,
                                   size_t* in_bytes_read)
{
  if (!m_started)
  {
    m_sha1_context = Common::SHA1::CreateContext();

    // Include the exception lists in the SHA-1 calculation (but not in the compression...)
    m_sha1_context->Update(in.data.data(), *in_bytes_read);

    m_started = true;
  }

  while (!m_done && in.bytes_written != *in_bytes_read &&
         (m_segment_bytes_written < sizeof(m_segment) || out->data.size() != out->bytes_written))
  {
    if (m_segment_bytes_written == 0 && *in_bytes_read == in.data.size() - Common::SHA1::DIGEST_LEN)
    {
      const size_t zeroes_to_write = std::min<size_t>(m_decompressed_size - m_out_bytes_written,
                                                       out->data.size() - out->bytes_written);

      std::memset(out->data.data() + out->bytes_written, 0, zeroes_to_write);

      out->bytes_written += zeroes_to_write;
      m_out_bytes_written += zeroes_to_write;

      if (m_out_bytes_written == m_decompressed_size && in.bytes_written == in.data.size())
      {
        const auto actual_hash = m_sha1_context->Finish();

        Common::SHA1::Digest expected_hash;
        std::memcpy(expected_hash.data(), in.data.data() + *in_bytes_read, expected_hash.size());

        *in_bytes_read += expected_hash.size();
        m_done = true;

        if (actual_hash != expected_hash)
          return false;
      }

      return true;
    }

    if (m_segment_bytes_written < sizeof(m_segment))
    {
      const size_t bytes_to_copy =
          std::min(in.bytes_written - *in_bytes_read, sizeof(m_segment) - m_segment_bytes_written);

      std::memcpy(reinterpret_cast<u8*>(&m_segment) + m_segment_bytes_written,
                  in.data.data() + *in_bytes_read, bytes_to_copy);
      m_sha1_context->Update(in.data.data() + *in_bytes_read, bytes_to_copy);

      *in_bytes_read += bytes_to_copy;
      m_bytes_read += bytes_to_copy;
      m_segment_bytes_written += bytes_to_copy;
    }

    if (m_segment_bytes_written < sizeof(m_segment))
      return true;

    const size_t offset = Common::swap32(m_segment.offset);
    const size_t size = Common::swap32(m_segment.size);

    if (m_out_bytes_written < offset)
    {
      const size_t zeroes_to_write =
          std::min(offset - m_out_bytes_written, out->data.size() - out->bytes_written);

      std::memset(out->data.data() + out->bytes_written, 0, zeroes_to_write);

      out->bytes_written += zeroes_to_write;
      m_out_bytes_written += zeroes_to_write;
    }

    if (m_out_bytes_written >= offset && m_out_bytes_written < offset + size)
    {
      const size_t bytes_to_copy = std::min(
          std::min(offset + size - m_out_bytes_written, out->data.size() - out->bytes_written),
          in.bytes_written - *in_bytes_read);

      std::memcpy(out->data.data() + out->bytes_written, in.data.data() + *in_bytes_read,
                  bytes_to_copy);
      m_sha1_context->Update(in.data.data() + *in_bytes_read, bytes_to_copy);

      *in_bytes_read += bytes_to_copy;
      m_bytes_read += bytes_to_copy;
      out->bytes_written += bytes_to_copy;
      m_out_bytes_written += bytes_to_copy;
    }

    if (m_out_bytes_written >= offset + size)
      m_segment_bytes_written = 0;
  }

  return true;
}

// STUB: libbz2 is not available in this standalone build, so bzip2-compressed
// blocks cannot be decoded. Always reports failure. (Codec unsupported, phase 1.)
bool Bzip2Decompressor::Decompress(const DecompressionBuffer& in, DecompressionBuffer* out,
                                   size_t* in_bytes_read)
{
  return false;
}

// STUB: the bundled "lzma" is the 7-Zip SDK, which is incompatible with the xz/liblzma
// API that Dolphin's real implementation relies on. The constructor is therefore a no-op
// and Decompress() always fails. (Codec intentionally unsupported in phase 1.)
LZMADecompressor::LZMADecompressor(bool lzma2, const u8* filter_options, size_t filter_options_size)
{
}

bool LZMADecompressor::Decompress(const DecompressionBuffer& in, DecompressionBuffer* out,
                                  size_t* in_bytes_read)
{
  return false;
}

ZstdDecompressor::ZstdDecompressor()
{
  m_stream = ZSTD_createDStream();
}

ZstdDecompressor::~ZstdDecompressor()
{
  ZSTD_freeDStream(m_stream);
}

bool ZstdDecompressor::Decompress(const DecompressionBuffer& in, DecompressionBuffer* out,
                                  size_t* in_bytes_read)
{
  if (!m_stream)
    return false;

  ZSTD_inBuffer in_buffer{in.data.data(), in.bytes_written, *in_bytes_read};
  ZSTD_outBuffer out_buffer{out->data.data(), out->data.size(), out->bytes_written};

  const size_t result = ZSTD_decompressStream(m_stream, &out_buffer, &in_buffer);

  *in_bytes_read = in_buffer.pos;
  out->bytes_written = out_buffer.pos;

  m_done = result == 0;
  return !ZSTD_isError(result);
}

RVZPackDecompressor::RVZPackDecompressor(std::unique_ptr<Decompressor> decompressor,
                                         DecompressionBuffer decompressed, u64 data_offset,
                                         u32 rvz_packed_size)
    : m_decompressor(std::move(decompressor)), m_decompressed(std::move(decompressed)),
      m_data_offset(data_offset), m_rvz_packed_size(rvz_packed_size)
{
  m_bytes_read = m_decompressed.bytes_written;
}

bool RVZPackDecompressor::IncrementBytesRead(size_t x)
{
  m_bytes_read += x;
  return m_bytes_read <= m_rvz_packed_size;
}

std::optional<bool> RVZPackDecompressor::ReadToDecompressed(const DecompressionBuffer& in,
                                                            size_t* in_bytes_read,
                                                            size_t decompressed_bytes_read,
                                                            size_t bytes_to_read)
{
  if (m_decompressed.data.size() < decompressed_bytes_read + bytes_to_read)
    m_decompressed.data.resize(decompressed_bytes_read + bytes_to_read);

  if (m_decompressed.bytes_written < decompressed_bytes_read + bytes_to_read)
  {
    const size_t prev_bytes_written = m_decompressed.bytes_written;

    if (!m_decompressor->Decompress(in, &m_decompressed, in_bytes_read))
      return false;

    if (!IncrementBytesRead(m_decompressed.bytes_written - prev_bytes_written))
      return false;

    if (m_decompressed.bytes_written < decompressed_bytes_read + bytes_to_read)
      return true;
  }

  return std::nullopt;
}

bool RVZPackDecompressor::Decompress(const DecompressionBuffer& in, DecompressionBuffer* out,
                                     size_t* in_bytes_read)
{
  while (out->data.size() != out->bytes_written && !Done())
  {
    if (m_size == 0)
    {
      if (m_decompressed.bytes_written == m_decompressed_bytes_read)
      {
        m_decompressed.data.resize(sizeof(u32));
        m_decompressed.bytes_written = 0;
        m_decompressed_bytes_read = 0;
      }

      std::optional<bool> result =
          ReadToDecompressed(in, in_bytes_read, m_decompressed_bytes_read, sizeof(u32));
      if (result)
        return *result;

      const u32 size = Common::swap32(m_decompressed.data.data() + m_decompressed_bytes_read);

      m_junk = size & 0x80000000;
      if (m_junk)
      {
        constexpr size_t SEED_SIZE = LaggedFibonacciGenerator::SEED_SIZE * sizeof(u32);
        constexpr size_t BLOCK_SIZE = 0x8000;

        result = ReadToDecompressed(in, in_bytes_read, m_decompressed_bytes_read + sizeof(u32),
                                    SEED_SIZE);
        if (result)
          return *result;

        m_lfg.SetSeed(m_decompressed.data.data() + m_decompressed_bytes_read + sizeof(u32));
        m_lfg.Forward(m_data_offset % BLOCK_SIZE);

        m_decompressed_bytes_read += SEED_SIZE;
      }

      m_decompressed_bytes_read += sizeof(u32);
      m_size = size & 0x7FFFFFFF;
    }

    size_t bytes_to_write = std::min<size_t>(m_size, out->data.size() - out->bytes_written);
    if (m_junk)
    {
      m_lfg.GetBytes(bytes_to_write, out->data.data() + out->bytes_written);
      out->bytes_written += bytes_to_write;
    }
    else
    {
      if (m_decompressed.bytes_written != m_decompressed_bytes_read)
      {
        bytes_to_write =
            std::min(bytes_to_write, m_decompressed.bytes_written - m_decompressed_bytes_read);

        std::memcpy(out->data.data() + out->bytes_written,
                    m_decompressed.data.data() + m_decompressed_bytes_read, bytes_to_write);

        m_decompressed_bytes_read += bytes_to_write;
        out->bytes_written += bytes_to_write;
      }
      else
      {
        const size_t prev_out_bytes_written = out->bytes_written;
        const size_t old_out_size = out->data.size();
        const size_t new_out_size = out->bytes_written + bytes_to_write;

        if (new_out_size < old_out_size)
          out->data.resize(new_out_size);

        if (!m_decompressor->Decompress(in, out, in_bytes_read))
          return false;

        out->data.resize(old_out_size);

        bytes_to_write = out->bytes_written - prev_out_bytes_written;

        if (!IncrementBytesRead(bytes_to_write))
          return false;

        if (bytes_to_write == 0)
          return true;
      }
    }

    m_data_offset += bytes_to_write;
    m_size -= static_cast<u32>(bytes_to_write);
  }

  // If out is full but not all data has been read from in, give the decompressor a chance to read
  // from in anyway. This is needed for the case where zstd has read everything except the checksum.
  if (out->data.size() == out->bytes_written && in.bytes_written != *in_bytes_read)
  {
    if (!m_decompressor->Decompress(in, out, in_bytes_read))
      return false;
  }

  return true;
}

bool RVZPackDecompressor::Done() const
{
  return m_size == 0 && m_rvz_packed_size == m_bytes_read &&
         m_decompressed.bytes_written == m_decompressed_bytes_read && m_decompressor->Done();
}

}  // namespace DiscIO

// Bridges rcheevos' rc_hash_filereader onto the ported Dolphin RVZ/WIA BlobReader: every read
// returns bytes of the logical (decompressed) disc image, so rcheevos hashes it like a plain ISO.
// Same role as HashCHD.cpp, but for RVZ/WIA (which GameCube/Wii hashing reads via the filereader,
// not the cdreader).
#include "HashRVZ.h"

#include <cstdio>
#include <cstring>
#include <memory>

#include "Common/CommonTypes.h"
#include "Common/DirectIOFile.h"
#include "DiscIO/Blob.h"
#include "DiscIO/WIABlob.h"

extern "C" {
#include "rc_hash.h"
}

namespace
{
struct RvzHandle
{
  std::unique_ptr<DiscIO::BlobReader> reader;
  s64 pos = 0;
};

void* rvz_open(const char* path)
{
  File::DirectIOFile probe(path, File::AccessMode::Read);
  if (!probe.IsOpen())
    return nullptr;
  u8 magic[4] = {0};
  probe.OffsetRead(0, magic, 4);
  const u32 m = (static_cast<u32>(magic[0]) << 24) | (static_cast<u32>(magic[1]) << 16) |
                (static_cast<u32>(magic[2]) << 8) | static_cast<u32>(magic[3]);

  File::DirectIOFile file(path, File::AccessMode::Read);
  std::unique_ptr<DiscIO::BlobReader> reader;
  if (m == 0x52565A01)  // "RVZ\x01"
    reader = DiscIO::RVZFileReader::Create(std::move(file), path);
  else if (m == 0x57494101)  // "WIA\x01"
    reader = DiscIO::WIAFileReader::Create(std::move(file), path);
  else
    return nullptr;

  if (!reader)
    return nullptr;
  auto* h = new RvzHandle();
  h->reader = std::move(reader);
  h->pos = 0;
  return h;
}

void rvz_seek(void* file_handle, int64_t offset, int origin)
{
  auto* h = static_cast<RvzHandle*>(file_handle);
  switch (origin)
  {
  case SEEK_SET:
    h->pos = offset;
    break;
  case SEEK_CUR:
    h->pos += offset;
    break;
  case SEEK_END:
    h->pos = static_cast<s64>(h->reader->GetDataSize()) + offset;
    break;
  default:
    break;
  }
}

int64_t rvz_tell(void* file_handle)
{
  return static_cast<RvzHandle*>(file_handle)->pos;
}

size_t rvz_read(void* file_handle, void* buffer, size_t requested_bytes)
{
  auto* h = static_cast<RvzHandle*>(file_handle);
  const u64 size = h->reader->GetDataSize();
  if (h->pos < 0 || static_cast<u64>(h->pos) >= size)
    return 0;

  size_t to_read = requested_bytes;
  if (static_cast<u64>(h->pos) + to_read > size)
    to_read = static_cast<size_t>(size - static_cast<u64>(h->pos));
  if (to_read == 0)
    return 0;

  if (!h->reader->Read(static_cast<u64>(h->pos), to_read, static_cast<u8*>(buffer)))
    return 0;
  h->pos += static_cast<s64>(to_read);
  return to_read;
}

void rvz_close(void* file_handle)
{
  delete static_cast<RvzHandle*>(file_handle);
}
}  // namespace

void rc_hash_init_rvz_filereader()
{
  rc_hash_filereader filereader;
  std::memset(&filereader, 0, sizeof(filereader));
  filereader.open = rvz_open;
  filereader.seek = rvz_seek;
  filereader.tell = rvz_tell;
  filereader.read = rvz_read;
  filereader.close = rvz_close;
  rc_hash_init_custom_filereader(&filereader);
}

int rc_hash_rvz_detect_console(const char* path)
{
  // The disc_header (first 0x80 bytes of the disc) and disc_type are stored uncompressed in the
  // RVZ/WIA header, so we can identify the console without decompressing anything.
  // WIAHeader1 is 0x48 bytes; WIAHeader2.disc_type is the first u32 right after it (offset 0x48),
  // big-endian: 1 = GameCube, 2 = Wii (per Dolphin docs/WiaAndRvz.md).
  File::DirectIOFile file(path, File::AccessMode::Read);
  if (!file.IsOpen())
    return 0;

  u8 header[0x4C] = {0};
  if (!file.OffsetRead(0, header, sizeof(header)))
    return 0;

  const u32 magic = (static_cast<u32>(header[0]) << 24) | (static_cast<u32>(header[1]) << 16) |
                    (static_cast<u32>(header[2]) << 8) | static_cast<u32>(header[3]);
  if (magic != 0x52565A01 && magic != 0x57494101)  // "RVZ\x01" / "WIA\x01"
    return 0;

  const u32 disc_type = (static_cast<u32>(header[0x48]) << 24) |
                        (static_cast<u32>(header[0x49]) << 16) |
                        (static_cast<u32>(header[0x4A]) << 8) | static_cast<u32>(header[0x4B]);
  if (disc_type == 1)
    return RC_CONSOLE_GAMECUBE;
  if (disc_type == 2)
    return RC_CONSOLE_WII;
  return 0;
}

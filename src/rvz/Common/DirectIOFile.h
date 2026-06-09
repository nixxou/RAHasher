// Shim for Dolphin's Common/DirectIOFile.h — backed by a C FILE* (single-threaded use).
#pragma once
#include <span>
#include <string>

#include "Common/CommonTypes.h"

namespace File
{
enum class SeekOrigin
{
  Begin,
  Current,
  End,
};

enum class AccessMode
{
  Read,
  Write,
  ReadAndWrite,
};

enum class OpenMode
{
  Default,
  Always,
  Truncate,
  Existing,
  Create,
};

// Minimal positioned-read file wrapper. OffsetRead ignores the current position and is the
// primitive used by the RVZ reader; Read/Seek/Tell track a logical position for convenience.
class DirectIOFile final
{
public:
  DirectIOFile();
  ~DirectIOFile();

  DirectIOFile(const DirectIOFile&);
  DirectIOFile& operator=(const DirectIOFile&);
  DirectIOFile(DirectIOFile&&) noexcept;
  DirectIOFile& operator=(DirectIOFile&&) noexcept;

  explicit DirectIOFile(const std::string& path, AccessMode access_mode,
                        OpenMode open_mode = OpenMode::Default);

  bool Open(const std::string& path, AccessMode access_mode, OpenMode open_mode = OpenMode::Default);
  bool Close();
  bool IsOpen() const;

  bool OffsetRead(u64 offset, u8* out_ptr, u64 size);
  bool OffsetRead(u64 offset, std::span<u8> out_data)
  {
    return OffsetRead(offset, out_data.data(), out_data.size());
  }

  bool Read(u8* out_ptr, u64 size)
  {
    if (!OffsetRead(m_current_offset, out_ptr, size))
      return false;
    m_current_offset += size;
    return true;
  }
  bool Read(std::span<u8> out_data) { return Read(out_data.data(), out_data.size()); }

  // Write side is unused by the reader but kept so the API matches.
  bool OffsetWrite(u64 offset, const u8* in_ptr, u64 size);
  bool Write(const u8* in_ptr, u64 size)
  {
    if (!OffsetWrite(m_current_offset, in_ptr, size))
      return false;
    m_current_offset += size;
    return true;
  }
  bool Write(std::span<const u8> in_data) { return Write(in_data.data(), in_data.size()); }

  u64 GetSize() const;
  bool Seek(s64 offset, SeekOrigin origin);
  u64 Tell() const { return m_current_offset; }
  bool Flush();

private:
  std::string m_path;
  void* m_handle = nullptr;  // FILE*
  u64 m_current_offset = 0;
  AccessMode m_access_mode = AccessMode::Read;
};
}  // namespace File

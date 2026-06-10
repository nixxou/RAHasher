/*
SevenZipArchive - lists and extracts archive entries (zip/rar/7z/...) into memory
using the 7-Zip library (7z.dll), so their contents can be hashed without touching
the disk.

The DLL is located at runtime (no link-time dependency). Search order:
  1. %RAHASHER_7Z_DLL% (full path to a 7z.dll, e.g. the 7-Zip-zstd build)
  2. 7z.dll sitting next to the RAHasher executable
  3. the 7-Zip install path from the registry (HKLM\SOFTWARE\7-Zip)
  4. C:\Program Files\7-Zip\7z.dll then C:\Program Files (x86)\7-Zip\7z.dll
  5. plain LoadLibrary("7z.dll") (relies on the system PATH)
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sevenzip
{
  /* metadata about one file entry inside an archive (directories excluded) */
  struct EntryInfo
  {
    uint32_t    index;   /* item index within the archive */
    std::string name;    /* full in-archive path */
    uint64_t    size;    /* uncompressed size (64-bit) */
    uint32_t    crc;     /* CRC32 stored in the archive (valid only if hasCrc) */
    bool        hasCrc;  /* whether the archive provided a CRC32 for this entry */
  };

  /* an entry extracted fully into memory */
  struct ExtractedEntry
  {
    EntryInfo            info;
    std::vector<uint8_t> data;
  };

  /* Invoked once per streamed entry, with its metadata and decompressed bytes held
   * in memory. Return false to abort the extraction. */
  typedef std::function<bool(const EntryInfo& info, const uint8_t* data, size_t size)> EntryDataCallback;

  /* True if `extWithDot` (e.g. ".7z", as returned by util::extension) is an
   * archive format we expand into its individual files. Case-insensitive. */
  bool isArchiveExtension(const std::string& extWithDot);

  /* An opened archive: enumerate its entries once, then extract any subset of them
   * (possibly several times) without re-opening. Create with open(); delete to close. */
  class Archive
  {
  public:
    /* Opens and enumerates the archive. Returns NULL on failure (sets `error`). */
    static Archive* open(const std::string& path, std::string& error);
    ~Archive();

    /* All file entries (directories excluded), in archive order. */
    const std::vector<EntryInfo>& entries() const { return _entries; }

    /* Decompress the given indices one at a time, streaming each through `cb`
     * (only one entry resides in memory at once). Indices are sorted/deduped. */
    bool extract(const std::vector<uint32_t>& indices, const EntryDataCallback& cb, std::string& error);

    /* Decompress the given indices fully into memory, appending one ExtractedEntry
     * per index to `out` (all kept resident - use for a .cue plus its tracks). */
    bool extractOwned(const std::vector<uint32_t>& indices, std::vector<ExtractedEntry>& out, std::string& error);

  private:
    Archive();
    Archive(const Archive&);
    Archive& operator=(const Archive&);

    void* _archive;  /* IInArchive*      */
    void* _stream;   /* IInStream* (file) */
    std::vector<EntryInfo> _entries;
  };
}

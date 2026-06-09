// Shim for Dolphin's DiscIO/Blob.h — just the BlobReader interface the RVZ reader implements.
#pragma once
#include <memory>
#include <optional>
#include <string>

#include "Common/CommonTypes.h"
#include "Common/Swap.h"

namespace DiscIO
{
enum class WIARVZCompressionType : u32;

enum class BlobType
{
  PLAIN,
  DRIVE,
  DIRECTORY,
  GCZ,
  CISO,
  WBFS,
  TGC,
  WIA,
  RVZ,
  MOD_DESCRIPTOR,
  NFS,
  SPLIT_PLAIN,
};

enum class DataSizeType
{
  Accurate,
  LowerBound,
  UpperBound,
};

std::string GetName(BlobType blob_type, bool translate);

class BlobReader
{
public:
  virtual ~BlobReader() = default;

  virtual BlobType GetBlobType() const = 0;
  virtual std::unique_ptr<BlobReader> CopyReader() const = 0;

  virtual u64 GetRawSize() const = 0;
  virtual u64 GetDataSize() const = 0;
  virtual DataSizeType GetDataSizeType() const = 0;

  virtual u64 GetBlockSize() const = 0;
  virtual bool HasFastRandomAccessInBlock() const = 0;
  virtual std::string GetCompressionMethod() const = 0;
  virtual std::optional<int> GetCompressionLevel() const = 0;

  virtual bool Read(u64 offset, u64 size, u8* out_ptr) = 0;
  template <typename T>
  std::optional<T> ReadSwapped(u64 offset)
  {
    T temp;
    if (!Read(offset, sizeof(T), reinterpret_cast<u8*>(&temp)))
      return std::nullopt;
    return Common::FromBigEndian(temp);
  }

  virtual bool SupportsReadWiiDecrypted(u64 offset, u64 size, u64 partition_data_offset) const
  {
    return false;
  }
  virtual bool ReadWiiDecrypted(u64 offset, u64 size, u8* out_ptr, u64 partition_data_offset)
  {
    return false;
  }

  virtual bool IsCached() const { return false; }

protected:
  BlobReader() {}
};
}  // namespace DiscIO

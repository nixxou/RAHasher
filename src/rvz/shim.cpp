// Implementations backing the Dolphin shim headers: a public-domain SHA-1, a FILE*-backed
// DirectIOFile, and DiscIO::GetName. Compiled into the standalone RVZ hashing harness.

// Request 64-bit file offsets on 32-bit POSIX (must precede any libc header include).
#if !defined(_WIN32)
#  define _FILE_OFFSET_BITS 64
#endif

#include "Common/Crypto/SHA1.h"
#include "Common/DirectIOFile.h"
#include "DiscIO/Blob.h"

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// SHA-1 (canonical reference implementation, public domain)
// ---------------------------------------------------------------------------
namespace
{
#define SHA1_ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

struct SHA1State
{
  u32 h[5];
  u64 length;  // total message length in bytes
  u8 buffer[64];
  size_t buffer_len;
};

void SHA1Init(SHA1State* s)
{
  s->h[0] = 0x67452301;
  s->h[1] = 0xEFCDAB89;
  s->h[2] = 0x98BADCFE;
  s->h[3] = 0x10325476;
  s->h[4] = 0xC3D2E1F0;
  s->length = 0;
  s->buffer_len = 0;
}

void SHA1Block(u32 h[5], const u8 block[64])
{
  u32 w[80];
  for (int i = 0; i < 16; i++)
  {
    w[i] = (static_cast<u32>(block[i * 4]) << 24) | (static_cast<u32>(block[i * 4 + 1]) << 16) |
           (static_cast<u32>(block[i * 4 + 2]) << 8) | static_cast<u32>(block[i * 4 + 3]);
  }
  for (int i = 16; i < 80; i++)
    w[i] = SHA1_ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

  u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
  for (int i = 0; i < 80; i++)
  {
    u32 f, k;
    if (i < 20)
    {
      f = (b & c) | ((~b) & d);
      k = 0x5A827999;
    }
    else if (i < 40)
    {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1;
    }
    else if (i < 60)
    {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDC;
    }
    else
    {
      f = b ^ c ^ d;
      k = 0xCA62C1D6;
    }
    const u32 temp = SHA1_ROL(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = SHA1_ROL(b, 30);
    b = a;
    a = temp;
  }
  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
}

void SHA1Update(SHA1State* s, const u8* data, size_t len)
{
  s->length += len;
  while (len > 0)
  {
    const size_t take = (64 - s->buffer_len < len) ? (64 - s->buffer_len) : len;
    std::memcpy(s->buffer + s->buffer_len, data, take);
    s->buffer_len += take;
    data += take;
    len -= take;
    if (s->buffer_len == 64)
    {
      SHA1Block(s->h, s->buffer);
      s->buffer_len = 0;
    }
  }
}

void SHA1Final(SHA1State* s, u8 out[20])
{
  const u64 bit_len = s->length * 8;
  const u8 pad = 0x80;
  SHA1Update(s, &pad, 1);
  const u8 zero = 0x00;
  while (s->buffer_len != 56)
    SHA1Update(s, &zero, 1);
  u8 len_be[8];
  for (int i = 0; i < 8; i++)
    len_be[i] = static_cast<u8>(bit_len >> (56 - i * 8));
  SHA1Update(s, len_be, 8);
  // s->length now includes padding; ignore. h[] holds the digest.
  for (int i = 0; i < 5; i++)
  {
    out[i * 4 + 0] = static_cast<u8>(s->h[i] >> 24);
    out[i * 4 + 1] = static_cast<u8>(s->h[i] >> 16);
    out[i * 4 + 2] = static_cast<u8>(s->h[i] >> 8);
    out[i * 4 + 3] = static_cast<u8>(s->h[i]);
  }
}

class SHA1ContextImpl final : public Common::SHA1::Context
{
public:
  SHA1ContextImpl() { SHA1Init(&m_state); }
  void Update(const u8* msg, size_t len) override { SHA1Update(&m_state, msg, len); }
  Common::SHA1::Digest Finish() override
  {
    Common::SHA1::Digest digest{};
    SHA1Final(&m_state, digest.data());
    return digest;
  }
  bool HwAccelerated() const override { return false; }

private:
  SHA1State m_state;
};
}  // namespace

namespace Common::SHA1
{
std::unique_ptr<Context> CreateContext()
{
  return std::make_unique<SHA1ContextImpl>();
}

Digest CalculateDigest(const u8* msg, size_t len)
{
  auto ctx = CreateContext();
  ctx->Update(msg, len);
  return ctx->Finish();
}

std::string DigestToString(const Digest& digest)
{
  static constexpr char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(digest.size() * 2);
  for (u8 b : digest)
  {
    out += hex[b >> 4];
    out += hex[b & 0xf];
  }
  return out;
}

std::string DigestToSource(const Digest& digest)
{
  return DigestToString(digest);
}
}  // namespace Common::SHA1

// ---------------------------------------------------------------------------
// File::DirectIOFile — FILE*-backed positioned reads (single-threaded harness).
// ---------------------------------------------------------------------------
namespace File
{
namespace
{
// Portable 64-bit fseek/ftell (Windows uses _fseeki64/_ftelli64; POSIX uses fseeko/ftello with
// 64-bit off_t via _FILE_OFFSET_BITS above).
inline int rvz_fseek64(std::FILE* f, long long offset, int origin)
{
#if defined(_WIN32)
  return _fseeki64(f, offset, origin);
#else
  return fseeko(f, static_cast<off_t>(offset), origin);
#endif
}
inline long long rvz_ftell64(std::FILE* f)
{
#if defined(_WIN32)
  return _ftelli64(f);
#else
  return static_cast<long long>(ftello(f));
#endif
}
}  // namespace

DirectIOFile::DirectIOFile() = default;

DirectIOFile::DirectIOFile(const std::string& path, AccessMode access_mode, OpenMode open_mode)
{
  Open(path, access_mode, open_mode);
}

DirectIOFile::~DirectIOFile()
{
  Close();
}

DirectIOFile::DirectIOFile(const DirectIOFile& other)
{
  *this = other;
}

DirectIOFile& DirectIOFile::operator=(const DirectIOFile& other)
{
  if (this == &other)
    return *this;
  Close();
  m_access_mode = other.m_access_mode;
  m_current_offset = other.m_current_offset;
  if (!other.m_path.empty())
    Open(other.m_path, other.m_access_mode);
  return *this;
}

DirectIOFile::DirectIOFile(DirectIOFile&& other) noexcept
{
  *this = std::move(other);
}

DirectIOFile& DirectIOFile::operator=(DirectIOFile&& other) noexcept
{
  if (this == &other)
    return *this;
  Close();
  m_path = std::move(other.m_path);
  m_handle = other.m_handle;
  m_current_offset = other.m_current_offset;
  m_access_mode = other.m_access_mode;
  other.m_handle = nullptr;
  other.m_current_offset = 0;
  return *this;
}

bool DirectIOFile::Open(const std::string& path, AccessMode access_mode, OpenMode /*open_mode*/)
{
  Close();
  m_path = path;
  m_access_mode = access_mode;
  const char* mode = (access_mode == AccessMode::Read) ? "rb" : "r+b";
  m_handle = std::fopen(path.c_str(), mode);
  m_current_offset = 0;
  return m_handle != nullptr;
}

bool DirectIOFile::Close()
{
  if (m_handle)
  {
    std::fclose(static_cast<std::FILE*>(m_handle));
    m_handle = nullptr;
  }
  return true;
}

bool DirectIOFile::IsOpen() const
{
  return m_handle != nullptr;
}

bool DirectIOFile::OffsetRead(u64 offset, u8* out_ptr, u64 size)
{
  if (!m_handle)
    return false;
  if (size == 0)
    return true;
  if (rvz_fseek64(static_cast<std::FILE*>(m_handle), static_cast<long long>(offset), SEEK_SET) != 0)
    return false;
  return std::fread(out_ptr, 1, size, static_cast<std::FILE*>(m_handle)) == size;
}

bool DirectIOFile::OffsetWrite(u64 offset, const u8* in_ptr, u64 size)
{
  if (!m_handle)
    return false;
  if (size == 0)
    return true;
  if (rvz_fseek64(static_cast<std::FILE*>(m_handle), static_cast<long long>(offset), SEEK_SET) != 0)
    return false;
  return std::fwrite(in_ptr, 1, size, static_cast<std::FILE*>(m_handle)) == size;
}

u64 DirectIOFile::GetSize() const
{
  if (!m_handle)
    return 0;
  std::FILE* f = static_cast<std::FILE*>(m_handle);
  const long long cur = rvz_ftell64(f);
  rvz_fseek64(f, 0, SEEK_END);
  const long long end = rvz_ftell64(f);
  rvz_fseek64(f, cur, SEEK_SET);
  return end < 0 ? 0 : static_cast<u64>(end);
}

bool DirectIOFile::Seek(s64 offset, SeekOrigin origin)
{
  switch (origin)
  {
  case SeekOrigin::Begin:
    m_current_offset = static_cast<u64>(offset);
    break;
  case SeekOrigin::Current:
    m_current_offset += offset;
    break;
  case SeekOrigin::End:
    m_current_offset = GetSize() + offset;
    break;
  }
  return true;
}

bool DirectIOFile::Flush()
{
  if (m_handle)
    std::fflush(static_cast<std::FILE*>(m_handle));
  return true;
}
}  // namespace File

// ---------------------------------------------------------------------------
// DiscIO::GetName
// ---------------------------------------------------------------------------
namespace DiscIO
{
std::string GetName(BlobType blob_type, bool /*translate*/)
{
  switch (blob_type)
  {
  case BlobType::WIA:
    return "WIA";
  case BlobType::RVZ:
    return "RVZ";
  default:
    return "";
  }
}
}  // namespace DiscIO

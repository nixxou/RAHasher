// Shim for Dolphin's Common/Crypto/AES.h — AES-128 CBC, backed by a self-contained tiny-AES
// (see AES.cpp). Only what the Wii re-encryption path needs.
#pragma once
#include <cstddef>
#include <memory>

#include "Common/CommonTypes.h"

namespace Common::AES
{
enum class Mode
{
  Decrypt,
  Encrypt,
};

class Context
{
public:
  static constexpr size_t KEY_SIZE = 16;
  static constexpr size_t BLOCK_SIZE = 16;

  virtual ~Context() = default;

  // CBC. len must be a multiple of BLOCK_SIZE. iv may be null (treated as 16 zero bytes).
  // iv_out (if non-null) receives the final 16-byte CBC feedback. buf_in/buf_out may alias.
  virtual bool Crypt(const u8* iv, u8* iv_out, const u8* buf_in, u8* buf_out, size_t len) const = 0;

  bool Crypt(const u8* iv, const u8* buf_in, u8* buf_out, size_t len) const
  {
    return Crypt(iv, nullptr, buf_in, buf_out, len);
  }
  bool CryptIvZero(const u8* buf_in, u8* buf_out, size_t len) const
  {
    return Crypt(nullptr, nullptr, buf_in, buf_out, len);
  }
};

std::unique_ptr<Context> CreateContextEncrypt(const u8* key);
std::unique_ptr<Context> CreateContextDecrypt(const u8* key);

}  // namespace Common::AES

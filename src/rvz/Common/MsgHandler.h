// Shim for Dolphin's Common/MsgHandler.h — translation is identity here.
#pragma once
#include <string>

namespace Common
{
inline std::string GetStringT(const char* s) { return std::string(s); }
}  // namespace Common

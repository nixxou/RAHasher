// Shim for Dolphin's Common/Logging/Log.h — logging is a no-op in the standalone RVZ reader.
// The category token (e.g. DISCIO) and fmt-style args are never evaluated.
#pragma once

#define ERROR_LOG_FMT(category, ...) ((void)0)
#define WARN_LOG_FMT(category, ...) ((void)0)
#define INFO_LOG_FMT(category, ...) ((void)0)
#define DEBUG_LOG_FMT(category, ...) ((void)0)
#define GENERIC_LOG_FMT(category, level, ...) ((void)0)

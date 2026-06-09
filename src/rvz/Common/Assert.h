// Shim for Dolphin's Common/Assert.h — map to <cassert>. The category/message args are ignored.
#pragma once
#include <cassert>

#define ASSERT(cond) assert(cond)
#define ASSERT_MSG(category, cond, ...) assert(cond)
#define DEBUG_ASSERT(cond) assert(cond)
#define DEBUG_ASSERT_MSG(category, cond, ...) assert(cond)

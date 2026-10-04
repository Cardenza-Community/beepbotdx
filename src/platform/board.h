#pragma once
#if defined(DESKTOP_BUILD) || defined(NATIVE_TEST)
namespace Board { inline bool isCardenza() { return false; } }
#else
#include <M5Unified.h>
namespace Board { inline bool isCardenza() { return M5.isCardenza(); } }
#endif

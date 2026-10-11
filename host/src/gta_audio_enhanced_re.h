// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#pragma once
#include "gta_audio_re.h"

namespace gtare {
// Static exact-build guards; resolves pointers without invoking any game code.
bool ResolveEnhancedImage(Audio& out, void* image, void (*log)(const char*));
void InitEnhancedParams(void* params);
}

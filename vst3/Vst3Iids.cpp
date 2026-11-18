// Vst3Iids.cpp -- the SDK defines interface IIDs in vstinitiids.cpp, but that unit does
// not include the gui headers, so IPlugView/IPlugFrame iids are never emitted. Define
// the missing ones here rather than patching the SDK.

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"

namespace Steinberg {

DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)

} // namespace Steinberg

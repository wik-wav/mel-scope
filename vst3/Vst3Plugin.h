// Vst3Plugin.h -- the one symbol the module entry needs from the implementation.
#pragma once

#include "pluginterfaces/base/ipluginbase.h"

namespace melscope_vst3 {
// Internal linkage in every unit that includes this, which is what we want: the entry
// unit registers the class, the plugin unit builds its instances.
static const Steinberg::FUID kProcessorUID(0x4D656C53, 0x636F7065, 0x00010000, 0x00000001);

Steinberg::FUnknown* createProcessor(void*);
}

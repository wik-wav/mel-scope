// Vst3Entry.cpp -- module entry and class registration for the VST3 build.

#include "pluginterfaces/base/fplatform.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h" // kVstAudioEffectClass, kSimpleModeSupported
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/main/pluginfactory.h"

#include "Vst3Plugin.h"

#define kMelScopeVersion "1.3.0"
#define kMelScopeName "Mel Scope"

using namespace Steinberg;

BEGIN_FACTORY("wyash", "https://wyash.invalid", "mailto:wyash.invalid", PFactoryInfo::kNoFlags)

DEF_CLASS2(INLINE_UID_FROM_FUID(melscope_vst3::kProcessorUID), PClassInfo::kManyInstances,
           kVstAudioEffectClass, kMelScopeName, Vst::kSimpleModeSupported, "Analyzer",
           kMelScopeVersion, kVstVersionString, melscope_vst3::createProcessor)

END_FACTORY

// The SDK's dllmain.cpp calls these two when the library loads and unloads.
bool InitModule()
{
    return true;
}

bool DeinitModule()
{
    return true;
}

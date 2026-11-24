// Vst3Plugin.cpp -- VST3 build of Mel Scope.
//
// Same engine, same Win32/GDI+ editor, wrapped in the VST3 SDK's single-component
// model (one class is component + controller). The editor window is our existing
// Gui, created as a child of the HWND the host hands to IPlugView::attached.

#include "Gui.h"
#include "MelCore.h"
#include "Plugin.h"

// The SDK header renames setState/getState while it pulls in the interfaces, so it has
// to be included before any VST3 interface header or the override chain breaks.
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include "pluginterfaces/base/futils.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/vstspeaker.h"

// that header aliases setState/getState to the *editor* state methods; we override the
// component state, so drop the aliases
#undef setState
#undef getState

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace melscope_vst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;

#include "Vst3Plugin.h"

// ---------------------------------------------------------------- state blob
// Deliberately the same layout as the CLAP state, so settings travel between builds.
constexpr char kMagic[4] = { 'M', 'L', 'S', 'C' };
constexpr uint32_t kStateVersion = 2;

void writeState(melscope::App& app, IBStream* out)
{
    if (!out) return;
    uint32_t count = (uint32_t)mel::pCount;
    uint32_t version = kStateVersion;
    int32 written = 0;
    out->write((void*)kMagic, 4, &written);
    out->write(&version, 4, &written);
    out->write(&count, 4, &written);
    for (uint32_t i = 0; i < count; ++i) {
        float v = app.get((int)i);
        out->write(&i, 4, &written);
        out->write(&v, 4, &written);
    }
    const uint32_t tailMagic = 0x54535758u, tailCount = 4;
    const float tail[4] = { (float)app.editorWidth(), (float)app.editorHeight(), app.viewLow(),
                            app.viewHigh() };
    out->write(const_cast<uint32_t*>(&tailMagic), 4, &written);
    out->write(const_cast<uint32_t*>(&tailCount), 4, &written);
    out->write(const_cast<float*>(tail), sizeof tail, &written);
}

void readState(melscope::App& app, IBStream* in)
{
    if (!in) return;
    char magic[4] = {};
    uint32_t version = 0, count = 0;
    int32 got = 0;
    if (in->read(magic, 4, &got) != kResultOk || got != 4) return;
    if (std::memcmp(magic, kMagic, 4) != 0) return;
    if (in->read(&version, 4, &got) != kResultOk) return;
    if (in->read(&count, 4, &got) != kResultOk) return;
    count = std::min<uint32_t>(count, (uint32_t)mel::pCount);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t id = 0;
        float v = 0.0f;
        if (in->read(&id, 4, &got) != kResultOk || got != 4) return;
        if (in->read(&v, 4, &got) != kResultOk || got != 4) return;
        if (id < (uint32_t)mel::pCount) app.set((int)id, v, false);
    }
    unsigned char tailHead[8];
    if (in->read(tailHead, 8, &got) == kResultOk && got == 8) {
        uint32_t magic = 0, n = 0;
        std::memcpy(&magic, tailHead, 4);
        std::memcpy(&n, tailHead + 4, 4);
        if (magic == 0x54535758u && n == 4) {
            float tail[4] = {};
            if (in->read(tail, sizeof tail, &got) == kResultOk && got == (int32)sizeof(tail)) {
                app.setEditorSize((int)(tail[0] + 0.5f), (int)(tail[1] + 0.5f));
                app.setViewRange(tail[2], tail[3]);
            }
        }
    }
    app.applyIfNeeded();
}

// ---------------------------------------------------------------- parameter
// The App keeps the truth (the panel writes it directly); this class maps a VST3
// parameter onto it so automation, the panel and saved state cannot disagree.
class AppParameter : public Parameter {
public:
    AppParameter(melscope::App& app, int pid, const mel::ParamDef& def) : app_(app), pid_(pid), def_(&def)
    {
        std::memset(&info, 0, sizeof(info));
        // VST3 wants a UTF-16 title; the table is ASCII
        const char* nm = def.name;
        int n = 0;
        for (; nm[n] && n < 127; ++n) info.title[n] = (char16)nm[n];
        info.title[n] = 0;
        for (int k = 0; k < n && k < 7; ++k) info.shortTitle[k] = (char16)nm[k];
        info.shortTitle[std::min(7, n)] = 0;
        if (def.suffix) {
            int s = 0;
            for (const char* u = def.suffix; *u && s < 127; ++u) info.units[s++] = (char16)*u;
            info.units[s] = 0;
        }
        info.stepCount = 0;
        if (!def.choices.empty()) {
            info.stepCount = (int32)def.choices.size();
        } else if (def.stepped) {
            info.stepCount = (int32)(def.max - def.min);
        }
        info.defaultNormalizedValue = toNormalized(def.def);
        info.unitId = kRootUnitId;
        info.flags = ParameterInfo::kCanAutomate;
        if (!def.choices.empty()) info.flags |= ParameterInfo::kIsList;
    }

    ParamValue toNormalized(ParamValue plain) const
    {
        const double lo = def_->min, hi = def_->max;
        if (hi <= lo) return 0.0;
        return std::min(1.0, std::max(0.0, (plain - lo) / (hi - lo)));
    }
    ParamValue toPlainValue(ParamValue norm) const
    {
        const double lo = def_->min, hi = def_->max;
        double v = lo + std::min(1.0, std::max(0.0, norm)) * (hi - lo);
        if (def_->stepped) v = std::round(v);
        return v;
    }

    ParamValue PLUGIN_API getNormalized() const { return toNormalized(app_.get(pid_)); }

    bool PLUGIN_API setNormalized(ParamValue v) SMTG_OVERRIDE
    {
        const ParamValue plain = toPlainValue(v);
        if (app_.get(pid_) != (float)plain) {
            app_.set(pid_, (float)plain, false);
            return true;
        }
        return false;
    }

    void PLUGIN_API toString(ParamValue v, String128 string) const SMTG_OVERRIDE
    {
        const ParamValue plain = toPlainValue(v);
        char buf[64];
        if (!def_->choices.empty()) {
            const int idx = std::min<int>((int)def_->choices.size() - 1, std::max(0, (int)plain));
            std::snprintf(buf, sizeof buf, "%s", def_->choices[(size_t)idx]);
        } else if (std::fabs(def_->max - def_->min) < 3.0) {
            std::snprintf(buf, sizeof buf, "%.2f%s", (double)plain, def_->suffix ? def_->suffix : "");
        } else if (std::fabs(def_->max - def_->min) < 40.0) {
            std::snprintf(buf, sizeof buf, "%.1f%s", (double)plain, def_->suffix ? def_->suffix : "");
        } else {
            std::snprintf(buf, sizeof buf, "%.0f%s", (double)plain, def_->suffix ? def_->suffix : "");
        }
        int n = 0;
        for (; buf[n] && n < 127; ++n) string[n] = (char16)buf[n];
        string[n] = 0;
    }

    bool PLUGIN_API fromString(const TChar* string, ParamValue& v) const SMTG_OVERRIDE
    {
        char buf[128] = {};
        int n = 0;
        for (; string && string[n] && n < 127; ++n) buf[n] = (char)string[n];
        if (!def_->choices.empty()) {
            for (size_t i = 0; i < def_->choices.size(); ++i) {
                if (std::strcmp(buf, def_->choices[i]) == 0) {
                    v = toNormalized((ParamValue)i);
                    return true;
                }
            }
            return false;
        }
        v = toNormalized(std::atof(buf));
        return true;
    }

    int pid() const { return pid_; }

    // Called from the UI thread after the panel changed the value.
    void syncFromApp()
    {
        const ParamValue now = getNormalized();
        if (std::fabs(now - last_) > 1e-7) {
            last_ = now;
            changed_ = true;
        }
    }
    bool takeChanged()
    {
        const bool c = changed_;
        changed_ = false;
        return c;
    }

private:
    melscope::App& app_;
    int pid_;
    const mel::ParamDef* def_;
    ParamValue last_ = -1.0;
    bool changed_ = false;
};

// ---------------------------------------------------------------- view
class MelScopeView : public IPlugView {
public:
    explicit MelScopeView(melscope::App& app) : app_(app) {}
    ~MelScopeView() { destroy(); }

    // ---- IPlugView
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) SMTG_OVERRIDE
    {
        return (type && std::strcmp(type, kPlatformTypeHWND) == 0) ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API attached(void* parent, FIDString type) SMTG_OVERRIDE
    {
        if (!parent || !type || std::strcmp(type, kPlatformTypeHWND) != 0) return kResultFalse;
        parent_ = (HWND)parent;
        if (!gui_) gui_ = std::make_unique<melscope::Gui>(app_);
        const int w = app_.editorWidth() > 0 ? app_.editorWidth()
                                             : (int)std::lround(1180.0 * scale());
        const int h = app_.editorHeight() > 0 ? app_.editorHeight()
                                              : (int)std::lround(700.0 * scale());
        if (!gui_->create(parent_, w, h)) {
            gui_.reset();
            return kResultFalse;
        }
        gui_->setResizeCallback([this] {
            // the panel changed the UI scale: ask the host for a window that matches
            size_.right = (int)std::lround(1180.0 * scale());
            size_.bottom = (int)std::lround(700.0 * scale());
            if (frame_) frame_->resizeView(this, &size_);
        });
        size_.left = 0;
        size_.top = 0;
        size_.right = w;
        size_.bottom = h;
        return kResultOk;
    }

    tresult PLUGIN_API removed() SMTG_OVERRIDE
    {
        destroy();
        return kResultOk;
    }

    tresult PLUGIN_API onSize(ViewRect* newSize) SMTG_OVERRIDE
    {
        if (!newSize) return kInvalidArgument;
        size_ = *newSize;
        app_.setEditorSize(size_.right - size_.left, size_.bottom - size_.top);
        if (gui_ && gui_->hwnd()) {
            SetWindowPos(gui_->hwnd(), nullptr, 0, 0, size_.right - size_.left,
                         size_.bottom - size_.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return kResultOk;
    }

    tresult PLUGIN_API getSize(ViewRect* out) SMTG_OVERRIDE
    {
        if (!out) return kInvalidArgument;
        *out = size_;
        return kResultOk;
    }

    tresult PLUGIN_API canResize() SMTG_OVERRIDE { return kResultTrue; }

    tresult PLUGIN_API checkSizeConstraint(ViewRect* rect) SMTG_OVERRIDE
    {
        if (!rect) return kInvalidArgument;
        const int32 w = std::max<int32>(560, rect->right - rect->left);
        const int32 h = std::max<int32>(360, rect->bottom - rect->top);
        rect->left = 0;
        rect->top = 0;
        rect->right = w;
        rect->bottom = h;
        return kResultTrue;
    }

    tresult PLUGIN_API setFrame(IPlugFrame* frame) SMTG_OVERRIDE
    {
        frame_ = frame;
        return kResultOk;
    }

    // our window handles its own input, so these are only here to satisfy the interface
    tresult PLUGIN_API onWheel(float) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API onFocus(TBool) SMTG_OVERRIDE { return kResultOk; }

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) SMTG_OVERRIDE
    {
        if (!obj) return kInvalidArgument;
        *obj = nullptr;
        if (FUnknownPrivate::iidEqual(iid, IPlugView::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            addRef();
            return kResultOk;
        }
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() SMTG_OVERRIDE { return ++ref_; }
    uint32 PLUGIN_API release() SMTG_OVERRIDE
    {
        const uint32 r = --ref_;
        if (r == 0) delete this;
        return r;
    }

private:
    double scale() const { return std::max(0.5, app_.get(mel::pUiScale) / 100.0); }

    void destroy()
    {
        if (gui_) {
            gui_->destroy();
            gui_.reset();
        }
        parent_ = nullptr;
    }

    melscope::App& app_;
    std::unique_ptr<melscope::Gui> gui_;
    HWND parent_ = nullptr;
    ViewRect size_{ 0, 0, 1180, 700 };
    IPlugFrame* frame_ = nullptr;
    std::atomic<uint32> ref_{ 1 };
};

// ---------------------------------------------------------------- plugin
class MelScopeVST3 : public SingleComponentEffect {
public:
    MelScopeVST3();
    ~MelScopeVST3() override;

    static FUnknown* createInstance(void*) { return (IAudioProcessor*)new MelScopeVST3(); }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) SMTG_OVERRIDE;
    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE;
    tresult PLUGIN_API setProcessing(TBool state) SMTG_OVERRIDE;
    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE;
    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;
    IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE;
    tresult PLUGIN_API setParamNormalized(ParamID tag, ParamValue value) SMTG_OVERRIDE;

    melscope::App& app() { return app_; }

private:
    melscope::App app_;
    double sampleRate_ = 48000.0;
    std::vector<AppParameter*> params_;
    bool prepared_ = false;
};

MelScopeVST3::MelScopeVST3()
{
    const auto& defs = mel::paramDefs();
    params_.reserve((size_t)mel::pCount);
    for (int i = 0; i < (int)mel::pCount; ++i) params_.push_back(new AppParameter(app_, i, defs[(size_t)i]));
    // settings.ini, shared with the CLAP build
    static std::string settingsPath;
    {
        char* appdata = nullptr;
        size_t len = 0;
        if (_dupenv_s(&appdata, &len, "APPDATA") == 0 && appdata) {
            settingsPath = std::string(appdata) + "\\MelScope\\settings.ini";
            CreateDirectoryA((std::string(appdata) + "\\MelScope").c_str(), nullptr);
            free(appdata);
        }
    }
    if (!settingsPath.empty()) app_.setPluginPath(settingsPath.c_str());
    app_.loadSettings();
}

MelScopeVST3::~MelScopeVST3() = default;

tresult PLUGIN_API MelScopeVST3::initialize(FUnknown* context)
{
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addAudioInput(STR16("Input"), SpeakerArr::kStereo);
    // the reference input: an aux bus, optional, so a host shows it as a sidechain and
    // nothing breaks when it is not connected
    addAudioInput(STR16("Reference"), SpeakerArr::kStereo, BusTypes::kAux, BusInfo::kDefaultActive);
    addAudioOutput(STR16("Output"), SpeakerArr::kStereo);
    addEventInput(USTRING("Event Input"), 16);

    if (parameters.getParameterCount() == 0)
        for (auto* p : params_) parameters.addParameter(p);
    return kResultOk;
}

tresult PLUGIN_API MelScopeVST3::terminate() { return SingleComponentEffect::terminate(); }

tresult PLUGIN_API MelScopeVST3::setupProcessing(ProcessSetup& setup)
{
    if (setup.symbolicSampleSize != kSample32) return kResultFalse;
    sampleRate_ = setup.sampleRate;
    app_.setSampleRate(setup.sampleRate);
    prepared_ = true;
    return kResultOk;
}

tresult PLUGIN_API MelScopeVST3::setActive(TBool state)
{
    if (state) {
        // The CLAP shell starts the analyser from its pluginActivate; this shell has to
        // do the same or the editor draws its chrome over an empty plot.
        app_.applyAnalysis();
        app_.engine.start();
        app_.engineRef.start();
    } else {
        app_.clearHistory();
    }
    return kResultOk;
}

tresult PLUGIN_API MelScopeVST3::setProcessing(TBool) { return kResultOk; }

tresult PLUGIN_API MelScopeVST3::process(ProcessData& data)
{
    const int32 frames = data.numSamples;
    if (frames <= 0) return kResultOk;

    // MIDI: notes feed the trace drawn under the spectrum. Events are echoed to the
    // output list too, so downstream instruments still see what was played.
    if (data.inputEvents) {
        const int32 n = data.inputEvents->getEventCount();
        for (int32 i = 0; i < n; ++i) {
            Event e{};
            if (data.inputEvents->getEvent(i, e) != kResultOk) continue;
            const double t = app_.engine.latestTime()
                             + (sampleRate_ > 0.0 ? (double)e.sampleOffset / sampleRate_ : 0.0);
            if (e.type == Event::kNoteOnEvent) {
                if (e.noteOn.velocity > 0.0f) app_.notes.noteOn(e.noteOn.pitch, t);
                else app_.notes.noteOff(e.noteOn.pitch, t);
            } else if (e.type == Event::kNoteOffEvent) {
                app_.notes.noteOff(e.noteOff.pitch, t);
            }
            if (data.outputEvents) data.outputEvents->addEvent(e);
        }
    }

    // pass-through first: the analyser must never alter what the host hears
    for (int32 b = 0; b < data.numOutputs; ++b) {
        const int32 inBus = std::min(b, data.numInputs - 1);
        auto& out = data.outputs[b];
        const float* in[2] = { nullptr, nullptr };
        if (inBus >= 0) {
            auto& inb = data.inputs[inBus];
            for (int32 c = 0; c < std::min(inb.numChannels, 2); ++c) in[c] = inb.channelBuffers32[c];
            if (inb.silenceFlags != 0) in[0] = in[1] = nullptr;
        }
        for (int32 c = 0; c < out.numChannels; ++c) {
            float* dst = out.channelBuffers32[c];
            if (!dst) continue;
            const float* src = in[std::min<int32>(c, 1)];
            if (src) {
                if (src != dst) std::memcpy(dst, src, sizeof(float) * (size_t)frames);
            } else {
                std::memset(dst, 0, sizeof(float) * (size_t)frames);
            }
        }
        out.silenceFlags = (in[0] || in[1]) ? 0 : (uint64)-1; // silence: every channel
    }

    // the reference bus feeds its own engine
    if (data.numInputs > 1) {
        auto& rb = data.inputs[1];
        const int rn = std::min(8, (int)rb.numChannels);
        if (rn > 0) {
            const float* rch[8];
            int used = 0;
            for (int cc = 0; cc < rn; ++cc)
                if (rb.channelBuffers32[cc]) rch[used++] = rb.channelBuffers32[cc];
            if (used > 0) app_.engineRef.pushAudio(rch, used, frames);
        }
    }

    if (data.numInputs > 0) {
        auto& inb = data.inputs[0];
        const int n = std::min<int32>(inb.numChannels, 8);
        if (n > 0) {
            const float* chans[8];
            int32 used = 0;
            for (int32 c = 0; c < n; ++c) {
                if (inb.channelBuffers32[c]) chans[used++] = inb.channelBuffers32[c];
            }
            if (used > 0) app_.engine.pushAudio(chans, used, frames);
        }
    }

    app_.applyIfNeeded();
    return kResultOk;
}

tresult PLUGIN_API MelScopeVST3::getState(IBStream* state)
{
    if (!state) return kInvalidArgument;
    writeState(app_, state);
    return kResultOk;
}

tresult PLUGIN_API MelScopeVST3::setState(IBStream* state)
{
    if (!state) return kInvalidArgument;
    readState(app_, state);
    // keep the parameter cache the host reads in step with what we loaded
    for (auto* p : params_) setParamNormalized((ParamID)p->pid(), p->getNormalized());
    return kResultOk;
}

IPlugView* PLUGIN_API MelScopeVST3::createView(FIDString name)
{
    if (name && std::strcmp(name, ViewType::kEditor) != 0) return nullptr;
    return new MelScopeView(app_);
}

tresult PLUGIN_API MelScopeVST3::setParamNormalized(ParamID tag, ParamValue value)
{
    tresult r = SingleComponentEffect::setParamNormalized(tag, value);
    app_.applyIfNeeded();
    return r;
}

// the module entry registers this factory function
FUnknown* createProcessor(void*)
{
    return (IAudioProcessor*)new MelScopeVST3();
}

} // namespace melscope_vst3

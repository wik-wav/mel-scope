// Plugin.cpp -- CLAP plugin shell (entry point, params, audio ports, GUI, state).
#include "Colormaps.h"
#include "Gui.h"
#include "MelCore.h"
#include "Plugin.h"

#include <clap/clap.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace melscope {

// ===========================================================================
// App
// ===========================================================================
App::App()
{
    const auto& defs = mel::paramDefs();
    for (int i = 0; i < mel::pCount && i < (int)defs.size(); ++i) values_[i].store(defs[(size_t)i].def);
    applied_ = mel::AnalysisConfig();
    applied_.sampleRate = sampleRate_;
    loadSettings(); // remembered across restarts; a project's saved state still wins
}

App::~App()
{
    if (loadedSettings_) saveSettings();
}

// --------------------------------------------------------------- persistence
namespace {

std::string g_pluginDir;

// Settings live in %APPDATA%\MelScope, with fallbacks: %LOCALAPPDATA%, then next
// to the plugin itself. Resolved once, on first use.
bool probeWritable(const std::string& dir, std::string& outFile)
{
    CreateDirectoryA(dir.c_str(), nullptr);
    const std::string file = dir + "\\settings.ini";
    FILE* f = std::fopen(file.c_str(), "ab");
    if (!f) return false;
    std::fclose(f);
    outFile = file;
    return true;
}

const std::string& settingsPath()
{
    static std::string resolved;
    if (!resolved.empty()) return resolved;
    char buf[1024];
    DWORD n = GetEnvironmentVariableA("APPDATA", buf, (DWORD)sizeof(buf));
    if (n > 0 && n < (DWORD)sizeof(buf) && probeWritable(std::string(buf) + "\\MelScope", resolved))
        return resolved;
    n = GetEnvironmentVariableA("LOCALAPPDATA", buf, (DWORD)sizeof(buf));
    if (n > 0 && n < (DWORD)sizeof(buf) && probeWritable(std::string(buf) + "\\MelScope", resolved))
        return resolved;
    if (!g_pluginDir.empty() && probeWritable(g_pluginDir, resolved)) return resolved;
    resolved = ".\\MelScope\\settings.ini";
    return resolved;
}

} // namespace

void App::setPluginPath(const char* path)
{
    if (!path || !*path) return;
    std::string p(path);
    const size_t slash = p.find_last_of("\\/");
    g_pluginDir = (slash == std::string::npos) ? std::string(".") : p.substr(0, slash);
}

// ---------------------------------------------------------------- window state
void App::setEditorSize(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    const int oldW = editorW_.exchange(w);
    const int oldH = editorH_.exchange(h);
    if (oldW != w || oldH != h) settingsDirty_.store(true, std::memory_order_relaxed);
}

void App::setViewRange(float lo, float hi)
{
    if (!(hi > lo)) {
        viewLo_.store(0.0f, std::memory_order_relaxed);
        viewHi_.store(0.0f, std::memory_order_relaxed);
    } else {
        viewLo_.store(lo, std::memory_order_relaxed);
        viewHi_.store(hi, std::memory_order_relaxed);
    }
    settingsDirty_.store(true, std::memory_order_relaxed);
}

bool App::writeParams(const char* path) const
{
    if (!path || !*path) return false;
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const auto& defs = mel::paramDefs();
    std::fprintf(f, "# Mel Scope settings (key=value, one per line)\n");
    std::fprintf(f, "# version=2\n");
    for (int i = 0; i < (int)defs.size(); ++i)
        std::fprintf(f, "%s=%.6f\n", defs[(size_t)i].key, (double)values_[i].load(std::memory_order_relaxed));
    // window state: not parameters, so they are written explicitly
    std::fprintf(f, "editorw=%d\n", editorWidth());
    std::fprintf(f, "editorh=%d\n", editorHeight());
    std::fprintf(f, "viewlow=%.3f\n", (double)viewLow());
    std::fprintf(f, "viewhigh=%.3f\n", (double)viewHigh());
    std::fclose(f);
    return true;
}

bool App::readParams(const char* path)
{
    if (!path || !*path) return false;
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char line[512];
    int applied = 0;
    bool sawCmap = false, darkTheme = false;
    bool currentFormat = false;
    // v1 stored the FFT size as an index into a longer list (256 ... 32768). Those
    // indices now mean different sizes, so a v1 file is translated once on load.
    static const int kOldFft[] = { 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 };
    static const int kNewFft[] = { 2048, 4096, 8192, 16384 };
    while (std::fgets(line, sizeof line, f)) {
        if (line[0] == '#') {
            if (std::strstr(line, "version=2")) currentFormat = true;
            continue;
        }
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        if (std::strcmp(line, "editorw") == 0) { editorW_.store(std::max(0, std::atoi(eq + 1))); continue; }
        if (std::strcmp(line, "editorh") == 0) { editorH_.store(std::max(0, std::atoi(eq + 1))); continue; }
        if (std::strcmp(line, "viewlow") == 0) { viewLo_.store((float)std::atof(eq + 1)); continue; }
        if (std::strcmp(line, "viewhigh") == 0) { viewHi_.store((float)std::atof(eq + 1)); continue; }
        const int pid = mel::paramIndexForKey(line);
        if (pid < 0) continue;
        if (pid == mel::pCmap) sawCmap = true;
        const auto& def = mel::paramDefs()[(size_t)pid];
        float v = (float)std::atof(eq + 1);
        if (pid == mel::pFft && !currentFormat) {
            const int old = std::max(0, std::min(7, (int)(v + 0.5f)));
            const int size = kOldFft[old];
            int best = 0;
            for (int i = 0; i < 4; ++i)
                if (size >= kNewFft[i]) best = i;
            v = (float)best;
        }
        values_[pid].store(std::max(def.min, std::min(def.max, v)), std::memory_order_relaxed);
        if (pid == mel::pTheme && v >= 0.5f) darkTheme = true;
        ++applied;
    }
    std::fclose(f);
    // A file that predates the Theme parameter (or simply omits these keys) gets
    // the canvas pointed the way the theme says, instead of a bright canvas on a
    // dark interface.
    if (applied > 0) {

        if (darkTheme && !sawCmap) {
            const int cm = (int)(values_[mel::pCmap].load() + 0.5f);
            if (cm >= 0 && cm < mel::kNumMaps && mel::kMapIsLight[cm])
                values_[mel::pCmap].store(0.0f, std::memory_order_relaxed);
        }
        analysisDirty_.store(true, std::memory_order_relaxed);
        settingsDirty_.store(true, std::memory_order_relaxed);
    }
    return applied > 0;
}

void App::loadSettings()
{
    // Same parser as the Load button, so the theme/canvas normalisation applies
    // to the automatic file too.
    readParams(settingsPath().c_str());
    loadedSettings_ = true; // we may write it back
    analysisDirty_.store(true, std::memory_order_relaxed);
}
void App::saveSettings() const { writeParams(settingsPath().c_str()); }

void App::maybeSaveSettings()
{
    if (!settingsDirty_.exchange(false)) return;
    const double now = (double)GetTickCount64() / 1000.0;
    if (now - lastSave_.load() < 1.5) {
        settingsDirty_.store(true); // coalesce bursts of drags into one write
        return;
    }
    lastSave_.store(now);
    saveSettings();
}

void App::set(int pid, float plain, bool notifyHost)
{
    if (pid < 0 || pid >= mel::pCount) return;
    const auto& def = mel::paramDefs()[(size_t)pid];
    plain = std::max(def.min, std::min(def.max, plain));
    if (def.stepped) plain = std::round(plain);
    const float old = values_[pid].exchange(plain, std::memory_order_relaxed);
    if (std::fabs(old - plain) < 1e-9f) return;

    if (pid == mel::pFreeze) {
        // the actual freeze / resume happens in applyIfNeeded(), which runs on the
        // timer and also catches presets and state recalls
    }
    if (pid == mel::pChannel) engine.setInputChannel((int)plain);
    if (pid == mel::pFft || pid == mel::pOverlap || pid == mel::pBands || pid == mel::pWindow
        || pid == mel::pFmin || pid == mel::pFmax || pid == mel::pSmooth) {
        analysisDirty_.store(true, std::memory_order_relaxed);
    }
    settingsDirty_.store(true, std::memory_order_relaxed);
    // Switching the theme also points the canvas the same way, and picks a
    // colormap that suits it: dark goes back to the original Ember canvas.
    if (pid == mel::pTheme) {
        const bool dark = plain >= 0.5f;
        // Each theme owns its gradients, so switching picks that theme's default:
        // Ember for dark, Ice for light.
        values_[mel::pCmap].store(dark ? 0.0f : 7.0f, std::memory_order_relaxed);
    }
    if (notifyHost) notify(pid);
}

void App::resetAll()
{
    const auto& defs = mel::paramDefs();
    for (int i = 0; i < mel::pCount; ++i) values_[i].store(defs[(size_t)i].def);
    analysisDirty_.store(true, std::memory_order_relaxed);
}

void App::clearHistory()
{
    engine.clearHistory();
    engineRef.clearHistory();
}

void App::resetRange()
{
    const auto& defs = mel::paramDefs();
    const float nyquist = (float)(sampleRate_ * 0.5);
    const float hi = std::min(defs[mel::pFmax].max, nyquist);
    values_[mel::pFmin].store(defs[mel::pFmin].min, std::memory_order_relaxed);
    values_[mel::pFmax].store(std::max(defs[mel::pFmax].min, hi), std::memory_order_relaxed);
    analysisDirty_.store(true, std::memory_order_relaxed);
    settingsDirty_.store(true, std::memory_order_relaxed);
}

void App::setSampleRate(double sr)
{
    if (sr > 0) sampleRate_ = sr;
    engine.prepare(sampleRate_);
    engineRef.prepare(sampleRate_);
    haveApplied_ = false;
    analysisDirty_.store(true, std::memory_order_relaxed);
}

void App::applyIfNeeded()
{
    // Single source of truth: the parameter. Whatever wrote it (the stepper, a preset,
    // project state, Reset all) the analyser follows on the next tick.
    const bool wantFrozen = frozen();
    if (wantFrozen && !wasFrozen_) {
        frozenTime_ = engine.latestTime();
        engine.setFrozen(true);
        wasFrozen_ = true;
    } else if (!wantFrozen && wasFrozen_) {
        engine.setFrozen(false);
        engine.clearHistory();
        frozenTime_ = 0.0;
        wasFrozen_ = false;
    }
    if (!analysisDirty_.exchange(false, std::memory_order_relaxed)) return;
    applyAnalysis();
}

void App::applyAnalysis()
{
    int fftCount = 0;
    const int* ffts = mel::fftSizeTable(fftCount);
    int bandsCount = 0;
    const int* bands = mel::bandsTable(bandsCount);

    mel::AnalysisConfig c;
    c.fftSize = ffts[std::max(0, std::min(fftCount - 1, getInt(mel::pFft)))];
    c.overlapIndex = std::max(0, std::min(3, getInt(mel::pOverlap)));
    c.bands = bands[std::max(0, std::min(bandsCount - 1, getInt(mel::pBands)))];
    c.window = getInt(mel::pWindow);
    c.channel = getInt(mel::pChannel);
    c.fmin = get(mel::pFmin);
    c.fmax = get(mel::pFmax);
    c.smooth = get(mel::pSmooth);
    c.sampleRate = sampleRate_;

    // While frozen the image belongs to the user: a structural change would rebuild
    // the mel bank and throw it away, so it is deferred until they unfreeze.
    if (frozen() && haveApplied_) return;
    // While frozen the image belongs to the user: a structural change would rebuild
    // the mel bank and throw it away, so it is deferred until they unfreeze.
    if (frozen()) return;
    if (!haveApplied_ || !c.sameAnalysis(applied_)) {
        applied_ = c;
        haveApplied_ = true;
        engine.setInputChannel(c.channel);
        engine.setAnalysisConfig(c);
    engineRef.setAnalysisConfig(c);
    }
}

mel::LevelRange App::levels() const
{
    return mel::computeRange(get(mel::pRange), get(mel::pCeil), getBool(mel::pAutoCeil),
                             engine.autoCeilingDb());
}

mel::ViewParams App::viewParams() const
{
    mel::ViewParams vp;
    vp.mode = getInt(mel::pMode); // 0 = spectrum only, 1 = harmonic traces
    vp.cmap = getInt(mel::pCmap);
    vp.light = (getInt(mel::pTheme) == 0) ? 1 : 0; // the canvas always follows the theme
    vp.axis = getInt(mel::pAxis);
    vp.gamma = get(mel::pGamma);
    vp.tilt = get(mel::pTilt);
    vp.glow = get(mel::pGlow);
    vp.span = get(mel::pSpan);
    const mel::LevelRange lr = levels();
    vp.floorDb = lr.floorDb;
    vp.ceilDb = lr.ceilDb;
    vp.lineRange = get(mel::pLineRange);
    vp.now = frozen() ? frozenTime_ : engine.latestTime();
    {
        const auto& fr = engine.frames();
        if (fr.count > 1) {
            const double oldest = fr.timeAt(0);
            const double available = std::max(0.3, vp.now - oldest);
            if (vp.span > available) vp.span = (float)available;
        }
    }
    return vp;
}

void App::toggleFreeze()
{
    if (frozen()) set(mel::pFreeze, 0.0f);
    else set(mel::pFreeze, 1.0f);
}

void App::notify(int paramId)
{
    if (changeFn_) changeFn_(changeCtx_, paramId);
}

} // namespace melscope

// ===========================================================================
// CLAP shell
// ===========================================================================
namespace {

using namespace melscope;

const char* const kFeatures[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_ANALYZER,
                                  CLAP_PLUGIN_FEATURE_UTILITY, CLAP_PLUGIN_FEATURE_STEREO,
                                  CLAP_PLUGIN_FEATURE_MONO, nullptr };

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION_INIT,
    "com.wyash.melscope",
    "Mel Scope",
    "wyash",
    "https://example.invalid/melscope",
    nullptr,
    nullptr,
    "1.0.0",
    "Real-time mel spectrogram with adjustable resolution, range and colour map",
    kFeatures,
};

struct Plugin {
    clap_plugin_t api;
    const clap_host_t* host = nullptr;
    App app;
    std::unique_ptr<Gui> gui;
    bool active = false;
    bool guiCreated = false;
    uint32_t wantW = 0, wantH = 0; // size the host asked for before creating the editor
    std::string libPath;
};

Plugin* self(const clap_plugin_t* p) { return (Plugin*)p->plugin_data; }

bool equalsIgnoreCase(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == 0 && *b == 0;
}

// ------------------------------------------------------------------- params
void applyParamEvent(Plugin* pl, clap_id id, double value)
{
    if (id < (clap_id)mel::pCount) pl->app.set((int)id, (float)value, /*notifyHost*/ false);
}

void readEventList(Plugin* pl, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (!ev) continue;
        if (ev->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (ev->type == CLAP_EVENT_NOTE_ON || ev->type == CLAP_EVENT_NOTE_OFF) {
            const auto* n = reinterpret_cast<const clap_event_note_t*>(ev);
            const double t = pl->app.engine.latestTime()
                             + (pl->app.sampleRate() > 0
                                    ? (double)ev->time / pl->app.sampleRate()
                                    : 0.0);
            if (ev->type == CLAP_EVENT_NOTE_ON)
                pl->app.notes.noteOn((int)n->key, t);
            else
                pl->app.notes.noteOff((int)n->key, t);
            continue;
        }
        if (ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* pv = reinterpret_cast<const clap_event_param_value_t*>(ev);
            applyParamEvent(pl, pv->param_id, pv->value);
        }
    }
}

uint32_t paramsCount(const clap_plugin_t*) { return (uint32_t)mel::pCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    const auto& defs = mel::paramDefs();
    if (index >= (uint32_t)defs.size() || !info) return false;
    const auto& d = defs[index];
    std::memset(info, 0, sizeof(*info));
    info->id = (clap_id)index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (!d.choices.empty()) info->flags |= CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
    info->cookie = nullptr;
    std::snprintf(info->name, sizeof(info->name), "%s", d.name);
    std::snprintf(info->module, sizeof(info->module), "%s", d.group);
    info->min_value = d.min;
    info->max_value = d.max;
    info->default_value = d.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* p, clap_id id, double* out)
{
    if (id >= (clap_id)mel::pCount || !out) return false;
    *out = self(p)->app.get((int)id);
    return true;
}

bool paramsValueToText(const clap_plugin_t* p, clap_id id, double value, char* out, uint32_t cap)
{
    if (id >= (clap_id)mel::pCount || !out || cap == 0) return false;
    const auto& def = mel::paramDefs()[(size_t)id];
    if (!def.choices.empty()) {
        int i = (int)(value + 0.5);
        i = std::max(0, std::min((int)def.choices.size() - 1, i));
        std::snprintf(out, cap, "%s", def.choices[(size_t)i]);
        return true;
    }
    if (id == mel::pFmin || id == mel::pFmax) {
        if (value >= 1000.0) std::snprintf(out, cap, "%.2f kHz", value / 1000.0);
        else std::snprintf(out, cap, "%.0f Hz", value);
    } else if (id == mel::pSpan) {
        std::snprintf(out, cap, "%.2f s", value);
    } else if (id == mel::pTilt) {
        std::snprintf(out, cap, "%+.1f dB/oct", value);
    } else if (id == mel::pRange || id == mel::pCeil) {
        std::snprintf(out, cap, "%.0f dB", value);
    } else if (id == mel::pGlow || id == mel::pSmooth || id == mel::pKeyLines || id == mel::pGrain) {
        std::snprintf(out, cap, "%.0f %%", value * 100.0);
    } else {
        std::snprintf(out, cap, "%.2f", value);
    }
    (void)p;
    return true;
}

bool paramsTextToValue(const clap_plugin_t* p, clap_id id, const char* text, double* out)
{
    if (id >= (clap_id)mel::pCount || !text || !out) return false;
    const auto& def = mel::paramDefs()[(size_t)id];
    if (!def.choices.empty()) {
        for (size_t i = 0; i < def.choices.size(); ++i) {
            if (equalsIgnoreCase(text, def.choices[i])) {
                *out = (double)i;
                return true;
            }
        }
    }
    char* end = nullptr;
    const double v = std::strtod(text, &end);
    if (end == text) return false;
    *out = v;
    (void)p;
    return true;
}

void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*)
{
    Plugin* pl = self(p);
    readEventList(pl, in);
    // Deliberately no reconfiguration here: flush() runs on the audio thread.
    // The host calls on_main_thread() and the GUI timer does the work.
    if (pl->host && pl->host->request_callback) pl->host->request_callback(pl->host);
}

const clap_plugin_params_t kParams = {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush,
};

// -------------------------------------------------------------- audio ports
// Two inputs: the main one, and a reference the user can feed from another track.
uint32_t portsCount(const clap_plugin_t*, bool isInput) { return isInput ? 2u : 1u; }

bool portsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info) return false;
    std::memset(info, 0, sizeof(*info));
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    if (isInput) {
        if (index == 0) {
            info->id = 0;
            std::snprintf(info->name, sizeof(info->name), "%s", "Input");
            info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        } else if (index == 1) {
            info->id = 1;
            std::snprintf(info->name, sizeof(info->name), "%s", "Reference");
            info->flags = 0;
        } else {
            return false;
        }
        return true;
    }
    if (index != 0) return false;
    info->id = 2;
    std::snprintf(info->name, sizeof(info->name), "%s", "Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    return true;
}

const clap_plugin_audio_ports_t kAudioPorts = { portsCount, portsGet };

// ---------------------------------------------------------------------- gui
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    if (api) *api = CLAP_WINDOW_API_WIN32;
    if (isFloating) *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* p, const char* api, bool isFloating)
{
    Plugin* pl = self(p);
    if (!guiIsApiSupported(p, api, isFloating)) return false;
    if (!pl->gui) pl->gui.reset(new Gui(pl->app));
    return true;
}

void guiDestroy(const clap_plugin_t* p)
{
    Plugin* pl = self(p);
    if (pl->gui) {
        pl->gui->destroy();
        pl->gui.reset();
    }
}

bool guiSetScale(const clap_plugin_t* p, double scale)
{
    Plugin* pl = self(p);
    if (!pl->gui) return false;
    pl->gui->setScale(scale);
    return true;
}

// Preferred size for a newly opened editor: the design size at the user's UI scale.
void preferredSize(Plugin* pl, uint32_t* w, uint32_t* h)
{
    const int pct = std::max(50, std::min(300, pl->app.getInt(mel::pUiScale)));
    *w = (uint32_t)(1180 * pct / 100);
    *h = (uint32_t)(700 * pct / 100);
}

bool guiGetSize(const clap_plugin_t* p, uint32_t* width, uint32_t* height)
{
    Plugin* pl = self(p);
    if (!width || !height) return false;
    // Before the editor exists there is no "current" size: answer with the size it
    // will be created at, or the host sizes the window for 100 % and clips the real
    // one. The UI scale makes those two differ.
    if (!pl->guiCreated) {
        uint32_t pw = 1180, ph = 700;
        if (pl->app.editorWidth() > 0 && pl->app.editorHeight() > 0) {
            pw = (uint32_t)pl->app.editorWidth();
            ph = (uint32_t)pl->app.editorHeight();
        } else {
            preferredSize(pl, &pw, &ph);
        }
        *width = pw;
        *height = ph;
        return true;
    }
    int w = 1180, h = 700;
    if (pl->gui) pl->gui->getSize(w, h);
    *width = (uint32_t)std::max(320, w);
    *height = (uint32_t)std::max(240, h);
    return true;
}

void requestResize(Plugin* pl)
{
    if (!pl || !pl->host || !pl->host->get_extension) return;
    const auto* hg = (const clap_host_gui_t*)pl->host->get_extension(pl->host, CLAP_EXT_GUI);
    if (!hg || !hg->request_resize) return;
    uint32_t w = 1180, h = 700;
    if (pl->gui) pl->gui->getSize((int&)w, (int&)h);
    uint32_t pw = 1180, ph = 700;
    preferredSize(pl, &pw, &ph);
    hg->request_resize(pl->host, pw, ph);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    if (!hints) return false;
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = false;
    hints->aspect_ratio_width = 0;
    hints->aspect_ratio_height = 0;
    return true;
}

bool guiAdjustSize(const clap_plugin_t*, uint32_t* width, uint32_t* height)
{
    if (!width || !height) return false;
    if (*width < 720) *width = 720;
    if (*height < 440) *height = 440;
    return true;
}

bool guiSetSize(const clap_plugin_t* p, uint32_t width, uint32_t height)
{
    Plugin* pl = self(p);
    if (!pl->gui) return false;
    pl->wantW = width;
    pl->wantH = height;
    pl->app.setEditorSize((int)width, (int)height);
    pl->gui->setSize((int)width, (int)height);
    return true;
}

bool guiSetParent(const clap_plugin_t* p, const clap_window_t* window)
{
    Plugin* pl = self(p);
    if (!pl->gui || !window) return false;
    if (std::strcmp(window->api, CLAP_WINDOW_API_WIN32) != 0) return false;
    uint32_t w = 1180, h = 700;
    if (pl->wantW > 0 && pl->wantH > 0) {
        // the host already decided the editor size (many do, before creating it)
        w = pl->wantW;
        h = pl->wantH;
    } else {
        preferredSize(pl, &w, &h);
    }
    return pl->gui->create((HWND)window->win32, (int)w, (int)h);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }

void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* p)
{
    Plugin* pl = self(p);
    if (!pl->gui) return false;
    pl->gui->show();
    return true;
}

bool guiHide(const clap_plugin_t* p)
{
    Plugin* pl = self(p);
    if (!pl->gui) return false;
    pl->gui->hide();
    return true;
}

const clap_plugin_gui_t kGui = {
    guiIsApiSupported, guiGetPreferredApi, guiCreate,          guiDestroy,         guiSetScale,
    guiGetSize,        guiCanResize,       guiGetResizeHints,  guiAdjustSize,      guiSetSize,
    guiSetParent,      guiSetTransient,    guiSuggestTitle,    guiShow,            guiHide,
};

// -------------------------------------------------------------------- state
const char kStateMagic[4] = { 'M', 'L', 'S', 'C' };
// 2: the FFT list dropped everything below 2048 and 32768, so a stored index
// means a different size. Older blobs are refused and the defaults win.
const uint32_t kStateVersion = 2;

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* stream)
{
    Plugin* pl = self(p);
    if (!stream) return false;
    std::vector<unsigned char> buf;
    auto push = [&buf](const void* data, size_t n) {
        const unsigned char* c = (const unsigned char*)data;
        buf.insert(buf.end(), c, c + n);
    };
    const uint32_t version = kStateVersion;
    const uint32_t count = (uint32_t)mel::pCount;
    push(kStateMagic, 4);
    push(&version, 4);
    push(&count, 4);
    for (uint32_t i = 0; i < count; ++i) {
        const float v = pl->app.get((int)i);
        push(&i, 4);
        push(&v, 4);
    }
    // window state travels with the project: a blob without this tail simply keeps the
    // settings-file values
    const uint32_t tailMagic = 0x54535758u; // 'WSXT'
    const uint32_t tailCount = 4;
    const float tail[4] = { (float)pl->app.editorWidth(), (float)pl->app.editorHeight(),
                            pl->app.viewLow(), pl->app.viewHigh() };
    push(&tailMagic, 4);
    push(&tailCount, 4);
    push(tail, sizeof tail);
    const int64_t written = stream->write(stream, buf.data(), (uint64_t)buf.size());
    return written == (int64_t)buf.size();
}

bool stateLoad(const clap_plugin_t* p, const clap_istream_t* stream)
{
    Plugin* pl = self(p);
    if (!stream) return false;
    unsigned char header[12];
    int64_t got = stream->read(stream, header, sizeof(header));
    if (got != (int64_t)sizeof(header)) return false;
    if (std::memcmp(header, kStateMagic, 4) != 0) return false;
    uint32_t blobVersion = 0;
    std::memcpy(&blobVersion, header + 4, 4);
    // Never refuse a blob on version grounds: a host that is told "state failed to
    // load" may abandon the whole device. An older blob is translated instead.
    static const int kOldFft[] = { 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 };
    static const int kNewFft[] = { 2048, 4096, 8192, 16384 };
    const bool legacyFft = blobVersion < 2;
    uint32_t count = 0;
    std::memcpy(&count, header + 8, 4);
    if (count > (uint32_t)mel::pCount) count = (uint32_t)mel::pCount;
    for (uint32_t i = 0; i < count; ++i) {
        unsigned char rec[8];
        // A truncated blob keeps whatever was readable instead of failing the load:
        // a host that is told "state failed" may drop the device entirely.
        if (stream->read(stream, rec, sizeof(rec)) != (int64_t)sizeof(rec)) break;
        uint32_t id = 0;
        float v = 0;
        std::memcpy(&id, rec, 4);
        std::memcpy(&v, rec + 4, 4);
        if (id == (uint32_t)mel::pFft && legacyFft) {
            const int oldIndex = std::max(0, std::min(7, (int)(v + 0.5f)));
            const int size = kOldFft[oldIndex];
            int mapped = 0;
            for (int k = 0; k < 4; ++k)
                if (size >= kNewFft[k]) mapped = k;
            v = (float)mapped;
        }
        if (id < (uint32_t)mel::pCount) pl->app.set((int)id, v);
    }
    // optional window-state tail
    unsigned char tailHead[8];
    if (stream->read(stream, tailHead, sizeof(tailHead)) == (int64_t)sizeof(tailHead)) {
        uint32_t magic = 0, n = 0;
        std::memcpy(&magic, tailHead, 4);
        std::memcpy(&n, tailHead + 4, 4);
        if (magic == 0x54535758u && n == 4) {
            float tail[4] = {};
            if (stream->read(stream, tail, sizeof(tail)) == (int64_t)sizeof(tail)) {
                pl->app.setEditorSize((int)(tail[0] + 0.5f), (int)(tail[1] + 0.5f));
                pl->app.setViewRange(tail[2], tail[3]);
            }
        }
    }
    pl->app.applyIfNeeded();
    return true;
}

const clap_plugin_state_t kState = { stateSave, stateLoad };

// ------------------------------------------------------- debug stats extension
// Not a standard CLAP extension; hosts ignore it. It exists so the test harness
// (and a human with a debugger) can see that the analyser is actually running.
struct StatsV1 {
    uint32_t size;
    uint32_t frames;
    double time;
    uint32_t bands;
    uint32_t hop;
    uint32_t fft;
    double sampleRate;
    float autoCeil;
    int peakCount;
};

typedef struct melscope_debug_ext {
    uint32_t version;
    void(CLAP_ABI* get_stats)(const clap_plugin_t* plugin, StatsV1* out);
} melscope_debug_ext_t;

void debugGetStats(const clap_plugin_t* p, StatsV1* out)
{
    Plugin* pl = self(p);
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    out->size = sizeof(StatsV1);
    std::lock_guard<std::mutex> lk(pl->app.engine.lock());
    const auto& frames = pl->app.engine.frames();
    out->frames = (uint32_t)frames.count;
    out->time = pl->app.engine.latestTime();
    out->bands = (uint32_t)pl->app.engine.bank().bands;
    out->hop = (uint32_t)pl->app.engine.config().hopSize();
    out->fft = (uint32_t)pl->app.engine.config().fftSize;
    out->sampleRate = pl->app.engine.config().sampleRate;
    out->autoCeil = pl->app.engine.autoCeilingDb();
    out->peakCount = frames.count > 0 ? frames.peakCountAt(frames.count - 1) : 0;
}

const melscope_debug_ext_t kDebug = { 1, debugGetStats };

// ------------------------------------------------------------------- plugin
bool pluginInit(const clap_plugin_t* p)
{
    Plugin* pl = self(p);
    pl->app.setChangeCallback(
        [](void* ctx, int paramId) {
            Plugin* self_ = (Plugin*)ctx;
            if (paramId >= 0 && self_->host) {
                const auto* hp = (const clap_host_params_t*)self_->host->get_extension(
                    self_->host, CLAP_EXT_PARAMS);
                if (hp && hp->rescan) hp->rescan(self_->host, CLAP_PARAM_RESCAN_VALUES);
            }
        },
        pl);
    return true;
}

void pluginDestroy(const clap_plugin_t* p)
{
    delete self(p);
}

bool pluginActivate(const clap_plugin_t* p, double sampleRate, uint32_t, uint32_t maxFrames)
{
    Plugin* pl = self(p);
    pl->active = true;
    pl->app.setSampleRate(sampleRate);
    pl->app.engine.start();
    pl->app.engineRef.start();
    pl->app.applyIfNeeded();
    return true;
}

void pluginDeactivate(const clap_plugin_t* p)
{
    Plugin* pl = self(p);
    pl->active = false;
    pl->app.engine.stop();
}

bool pluginStartProcessing(const clap_plugin_t*) { return true; }
void pluginStopProcessing(const clap_plugin_t*) {}
void pluginReset(const clap_plugin_t*) {}

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* process)
{
    Plugin* pl = self(p);
    if (!pl->active || !process) return CLAP_PROCESS_CONTINUE;
    const uint32_t frames = process->frames_count;
    if (frames == 0) return CLAP_PROCESS_CONTINUE;

    if (process->in_events) readEventList(pl, process->in_events);

    const clap_audio_buffer_t* in = process->audio_inputs_count > 0 ? &process->audio_inputs[0] : nullptr;
    clap_audio_buffer_t* out = process->audio_outputs_count > 0 ? &process->audio_outputs[0] : nullptr;

    if (out && out->data32) {
        const uint32_t outCh = out->channel_count;
        const uint32_t inCh = in ? in->channel_count : 0;
        for (uint32_t c = 0; c < outCh; ++c) {
            float* dst = out->data32[c];
            if (!dst) continue;
            const float* src = (in && in->data32 && c < inCh) ? in->data32[c] : nullptr;
            if (src && src != dst) std::memcpy(dst, src, sizeof(float) * frames);
            else if (!src) std::memset(dst, 0, sizeof(float) * frames);
        }
    }

    // the reference input goes to its own engine
    if (process->audio_inputs_count > 1) {
        const clap_audio_buffer_t* ref = &process->audio_inputs[1];
        if (ref->data32 && ref->channel_count > 0 && ref->channel_count <= 8) {
            const float* rch[8];
            uint32_t rn = 0;
            for (uint32_t c = 0; c < ref->channel_count; ++c)
                if (ref->data32[c]) rch[rn++] = ref->data32[c];
            if (rn > 0) pl->app.engineRef.pushAudio(rch, (int)rn, (int)frames);
        }
    }
    if (in && in->data32 && in->channel_count > 0 && in->channel_count <= 8) {
        const float* chans[8];
        uint32_t n = 0;
        for (uint32_t c = 0; c < in->channel_count; ++c) {
            if (in->data32[c]) chans[n++] = in->data32[c];
        }
        if (n > 0) pl->app.engine.pushAudio(chans, (int)n, (int)frames);
    }

    return CLAP_PROCESS_CONTINUE;
}


// ---------------------------------------------------------------- note ports
// One MIDI input port: the plugin listens to what is played and never blocks it, since
// CLAP delivers events to every plugin on the track.
uint32_t notePortsCount(const clap_plugin_t* p, bool is_input)
{
    (void)p;
    return is_input ? 1u : 0u;
}

bool notePortsGet(const clap_plugin_t* p, uint32_t index, bool is_input, clap_note_port_info_t* info)
{
    (void)p;
    if (!is_input || index != 0 || !info) return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "%s", "Notes");
    return true;
}

const clap_plugin_note_ports_t kNotePorts = { notePortsCount, notePortsGet };

const void* pluginGetExtension(const clap_plugin_t* p, const char* id)
{
    (void)p;
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kParams;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &kNotePorts;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPorts;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &kGui;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &kState;
    if (std::strcmp(id, "com.wyash.melscope.debug") == 0) return &kDebug;
    return nullptr;
}

void pluginOnMainThread(const clap_plugin_t* p)
{
    Plugin* pl = self(p);
    pl->app.applyIfNeeded();
    if (pl->gui) pl->gui->markDirty();
}

// ------------------------------------------------------------------ factory
uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host,
                                         const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, kDescriptor.id) != 0) return nullptr;

    Plugin* pl = new Plugin();
    pl->host = host;
    pl->api.desc = &kDescriptor;
    pl->api.plugin_data = pl;
    pl->api.init = pluginInit;
    pl->api.destroy = pluginDestroy;
    pl->api.activate = pluginActivate;
    pl->api.deactivate = pluginDeactivate;
    pl->api.start_processing = pluginStartProcessing;
    pl->api.stop_processing = pluginStopProcessing;
    pl->api.reset = pluginReset;
    pl->api.process = pluginProcess;
    pl->api.get_extension = pluginGetExtension;
    pl->api.on_main_thread = pluginOnMainThread;
    return &pl->api;
}

const clap_plugin_factory_t kFactory = {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin,
};

} // namespace

// ===========================================================================
// entry point
// ===========================================================================
extern "C" CLAP_EXPORT bool clap_entry_init(const char* plugin_path)
{
    App::setPluginPath(plugin_path);
    return true;
}

extern "C" CLAP_EXPORT void clap_entry_deinit(void) {}

extern "C" CLAP_EXPORT const void* clap_entry_get_factory(const char* factory_id)
{
    if (factory_id && std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) return &kFactory;
    return nullptr;
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    clap_entry_init,
    clap_entry_deinit,
    clap_entry_get_factory,
};

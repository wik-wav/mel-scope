// Plugin.h -- shared state between the CLAP shell and the Win32 GUI.
#pragma once

#include "MelCore.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace melscope {

class Gui;

// Everything the GUI and the audio/analysis side share. All parameter access
// goes through atomics; the engine owns its own locking.
class App {
public:
    App();
    ~App();

    mel::Engine engine;

    float get(int pid) const { return values_[pid].load(std::memory_order_relaxed); }
    int getInt(int pid) const { return (int)(get(pid) + 0.5f); }
    bool getBool(int pid) const { return get(pid) >= 0.5f; }
    void set(int pid, float plain, bool notifyHost = true);
    void resetAll();
    // Low cut -> minimum, High cut -> Nyquist of the current sample rate.
    void resetRange();
    void clearHistory();

    // Rebuilds AnalysisConfig from the parameters and pushes it to the engine
    // when it actually changed (which clears the history).
    void applyAnalysis();
    void applyIfNeeded();

    // Persistent settings (survive the plugin, the DAW and a reboot).
    static void setPluginPath(const char* path);
    void loadSettings();
    // explicit preset files (Save / Load buttons in the panel)
    bool writeParams(const char* path) const;
    bool readParams(const char* path);
    void saveSettings() const;
    void maybeSaveSettings();
    void setSampleRate(double sr);

    // Absolute dB bounds for the current (range, ceiling, auto) parameters.
    mel::LevelRange levels() const;
    mel::ViewParams viewParams() const;

    // Host notification for parameter changes made by the GUI.
    void setChangeCallback(void (*fn)(void* ctx, int paramId), void* ctx)
    {
        changeFn_ = fn;
        changeCtx_ = ctx;
    }
    void notify(int paramId);

    double sampleRate() const { return sampleRate_; }
    double now() const { return engine.latestTime(); }

    bool frozen() const { return getBool(mel::pFreeze); }
    void toggleFreeze();

private:
    std::atomic<float> values_[mel::pCount];
    double sampleRate_ = 48000.0;
    mel::AnalysisConfig applied_;
    bool haveApplied_ = false;
    std::atomic<bool> analysisDirty_{false};
    std::atomic<bool> settingsDirty_{false};
    std::atomic<double> lastSave_{0.0};
    bool loadedSettings_ = false;
    double frozenTime_ = 0.0;
    bool wasFrozen_ = false;
    void (*changeFn_)(void*, int) = nullptr;
    void* changeCtx_ = nullptr;
};

} // namespace melscope

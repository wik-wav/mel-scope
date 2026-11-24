// MelCore.h -- analysis + rendering core shared by the CLAP plugin and the
// offline render tool. No UI, no host API: pure DSP.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mel {

// ---------------------------------------------------------------- parameters
// IMPORTANT: this order must stay identical to the order in makeParamDefs(),
// because parameter ids are indices into that table.
enum ParamIndex {
    pFft = 0,
    pOverlap,
    pBands,
    pWindow,
    pChannel,
    pFmin,
    pFmax,
    pRange,
    pCeil,
    pAutoCeil,
    pSpan,
    pMode,
    pTheme,
    pAxis,
    pCmap,
    pGamma,
    pTilt,
    pGlow,
    pGrain,
    pSmooth,
    pKeyLines,
    pFreeze,
    pUiScale, // appended last so existing parameter ids do not move
    pLineRange, // same rule: never insert, or saved values shift meaning
    pNotes,     // strength of the MIDI note layer (0 = off)
    pRefOn,     // reference input enabled
    pRefLevel,  // strength of the reference traces
    pRefInk,    // reference ink: auto (derived per map) or a fixed colour
    pRefBlend,  // how the two inks combine where both inputs have content
    pLegend,    // draw the key over the plot
    pCount
};

struct ParamDef {
    const char* key;                 // stable id, used by state blobs
    const char* name;                // short display name
    const char* group;               // control-panel group
    const char* desc;                // tooltip / status line
    float min, max, def;
    bool stepped;
    bool logarithmic;                // drag resolution should be exponential
    const char* suffix;              // " Hz", " dB", " s", ""
    std::vector<const char*> choices; // only for stepped params
};

const std::vector<ParamDef>& paramDefs();
// true when every ParamIndex matches its row in paramDefs() (guards reordering bugs)
bool paramTableIsConsistent();
int paramIndexForKey(const char* key);

// Exposed so the GUI can print e.g. "2048 / 512 hop" without duplicating tables.
const int* fftSizeTable(int& count);
const int* bandsTable(int& count);
const float* overlapTable(int& count);
const char* windowName(int i);
int windowCount();

// ------------------------------------------------------------------ utilities
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
double hzToMelSlaney(double hz);
double melToHzSlaney(double mel);

// ------------------------------------------------------------------------ FFT
class Fft {
public:
    void setup(int n);                 // n must be a power of two
    int size() const { return n_; }
    // in: n real samples. out: n/2+1 magnitudes of the normalised spectrum
    // (windowed, scaled so a full-scale sine peaks at 1.0).
    void magnitudes(const float* in, const float* window, float* out) const;

private:
    int n_ = 0, log2n_ = 0;
    mutable std::vector<float> re_, im_;
    std::vector<float> twCos_, twSin_;
    std::vector<int> bitrev_;
};

void makeWindow(int kind, int n, std::vector<float>& out); // sums the window too

// ------------------------------------------------------------ mel filterbank
struct MelBank {
    int bands = 0;
    int fftSize = 0;
    double sampleRate = 48000.0;
    float fmin = 30.0f, fmax = 16000.0f;
    std::vector<float> centerHz;
    std::vector<int> offset;   // bands+1
    std::vector<int> binStart; // per band
    std::vector<float> weight; // concatenated triangles

    void build(int bands_, int fftSize_, double sampleRate_, float fmin_, float fmax_);
    // power: fftSize/2+1 values; out: bands values
    void apply(const float* power, float* out) const;
    float bandToHz(float pos) const;   // pos in [0, bands-1]
    float hzToBand(float hz) const;    // -> position in [0, bands-1]
};

// Power spectrum from magnitudes (squares them).
void magnitudesToPower(float* mags, int count);

// ---------------------------------------------------------------- frame store
struct Peak {
    float hz;  // refined bin frequency
    float db;  // 20*log10 of the normalised magnitude
};

constexpr int kMaxPeaks = 96;

struct FrameStore {
    int bands = 0;
    std::vector<float> db;     // mel-band dB values, ring layout
    std::vector<double> times; // seconds, monotonic
    std::vector<Peak> peaks;   // cap * kMaxPeaks traced spectral peaks
    std::vector<uint16_t> peakCount;
    int head = 0;              // next write slot
    int count = 0;
    int cap = 0;

    void init(int bands_, int capacity);
    void clear();
    void push(const float* values, double time, const Peak* pks = nullptr, int nPeaks = 0);
    const float* frameAt(int i) const { return &db[(size_t)slot(i) * bands]; }
    const Peak* peaksAt(int i) const { return &peaks[(size_t)slot(i) * kMaxPeaks]; }
    int peakCountAt(int i) const { return (int)peakCount[(size_t)slot(i)]; }
    double timeAt(int i) const { return times[slot(i)]; }
    int slot(int i) const { int s = head - count + i; s %= cap; if (s < 0) s += cap; return s; }
    // index of the newest frame with time <= t (clamped to [0, count-1])
    int lowerBound(double t) const;
};

// --------------------------------------------------------------------- engine
struct AnalysisConfig {
    int fftSize = 2048;
    int overlapIndex = 1; // index into overlapTable()
    int bands = 192;
    int window = 0;
    int channel = 0; // 0 = mix, 1 = L, 2 = R
    float fmin = 30.0f, fmax = 16000.0f;
    float smooth = 0.25f;
    double sampleRate = 48000.0;

    int hopSize() const;
    // true when the analysis results would be identical (no rebuild needed)
    bool sameAnalysis(const AnalysisConfig& o) const;
};

// Single-threaded STFT + mel analyser. The plugin drives it from its analysis
// thread; the offline renderer drives it from the main thread. Same code.
class Analyzer {
public:
    void configure(const AnalysisConfig& cfg);
    void reset();

    // Feeds mono samples, appending finished frames to sink.
    void process(const float* mono, int numSamples, FrameStore& sink);

    const AnalysisConfig& config() const { return cfg_; }
    const MelBank& bank() const { return bank_; }
    float peakDb() const { return peakDb_; }
    std::int64_t samplesSeen() const { return samplesSeen_; }

private:
    void emitFrame(FrameStore& sink);

    AnalysisConfig cfg_;
    MelBank bank_;
    Fft fft_;
    std::vector<float> window_, buf_, mags_, hist_, smooth_, values_;
    std::vector<float> specDb_;
    std::vector<Peak> peaks_;
    int windowKind_ = -1;
    int hop_ = 512;
    int histPos_ = 0;
    int sinceFrame_ = 0;
    std::int64_t samplesSeen_ = 0;
    float peakDb_ = -20.0f;
};

class Engine {
public:
    Engine();
    ~Engine();

    void prepare(double sampleRate);
    void start();
    void stop();

    // audio thread -----------------------------------------------------
    void pushAudio(const float* const* channels, int numChannels, int numSamples);
    // 0 = mix (default), 1 = left, 2 = right. Safe to call from any thread.
    void setInputChannel(int c) { channel_.store(c, std::memory_order_relaxed); }

    // any thread -------------------------------------------------------
    void setAnalysisConfig(const AnalysisConfig& cfg); // reconfigures; clears history
    void clearHistory();
    // Freeze holds the frames themselves: the analysis thread stops consuming audio
    // instead of letting the ring overwrite the window being held.
    void setFrozen(bool f);
    bool frozen() const { return frozen_.load(std::memory_order_relaxed); }

    // reader thread ----------------------------------------------------
    std::mutex& lock() { return mu_; }
    const FrameStore& frames() const { return store_; }
    const MelBank& bank() const { return bank_; }
    const AnalysisConfig& config() const { return cfg_; }
    float autoCeilingDb() const { return autoCeil_.load(); }
    double latestTime() const { return latestTime_.load(); }
    bool running() const { return running_; }

private:
    void threadMain();
    void reconfigure(const AnalysisConfig& cfg); // analysis thread only
    int ringRead(float* dst, int maxCount);

    std::mutex mu_; // guards store_, bank_, cfg_
    FrameStore store_;
    MelBank bank_;
    AnalysisConfig cfg_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::atomic<float> autoCeil_{-12.0f};
    std::atomic<double> latestTime_{0.0};
    std::atomic<bool> frozen_{false};

    // audio ring (single producer / single consumer)
    std::vector<float> ring_;
    std::atomic<int> writePos_{0};
    std::atomic<int> readPos_{0};
    std::atomic<int> channel_{0};
    double sampleRate_ = 48000.0;

    // pending config, guarded by pendingMu_
    std::mutex pendingMu_;
    AnalysisConfig pending_;
    std::atomic<uint32_t> pendingEpoch_{0};
    uint32_t appliedEpoch_ = 0;
};

// ------------------------------------------------------------------ rendering
// One played note, in the same clock the image uses. `held` marks a note that has not
// been released yet, so it can be drawn to the right edge.
struct MidiNote {
    int pitch = 0;
    double start = 0.0;
    double end = 0.0;
    bool held = false;
};

// Note history: audio-rate note on/off in, UI-rate snapshot out. Fixed capacity so a
// long session cannot grow without bound; the oldest note is dropped first.
class NoteTrace {
public:
    static constexpr int kCapacity = 512;
    void noteOn(int pitch, double t);
    void noteOff(int pitch, double t);
    void clear();
    int count() const;
    // Copies the current notes in time order into out (held notes last).
    void snapshot(std::vector<MidiNote>& out) const;
    // Drops notes that ended before `before` so the ring does not hold ancient ones.
    void trim(double before);

private:
    mutable std::mutex mu_;
    std::vector<MidiNote> notes_; // ordered by start
};

struct ViewParams {
    int light = 1;   // 1 = light canvas (ramp reversed so quiet is pale)
    int mode = 1; // 0 = mel spectrum, 1 = harmonic traces (spectral peak tracing)
    int cmap = 0;
    int axis = 0;      // 0 mel, 1 log freq, 2 linear freq
    float floorDb = -95.0f;
    float ceilDb = -6.0f;
    // The peak traces have their own dB window: the spectrum can span 120 dB while
    // the lines only spread the top 60, which is what keeps quiet partials readable
    // as lighter lines instead of clipping to full ink.
    float lineRange = 60.0f;
    float gamma = 1.0f;
    float tilt = 0.0f;  // dB per octave around 1 kHz
    float glow = 0.0f;  // 0..1 -> blur radius
    float span = 4.0f;  // seconds visible across the width
    double now = 0.0;   // time at the right edge
    float fmin = 30.0f, fmax = 16000.0f;
    // Optional reference input, analysed separately and overlaid as its own traces.
    // refFrames == nullptr means the feature is off for this frame.
    const FrameStore* refFrames = nullptr;
    float refStrength = 0.85f;
    int refInk = 0;   // 0 auto, 1..4 fixed colours
    int refBlend = 4; // 0 mix, 1 xor, 2 screen, 3 third hue, 4 invert hue
    // played notes, blended into the image behind the spectrum; noteLevel 0 disables
    const MidiNote* notes = nullptr;
    int noteCount = 0;
    float noteLevel = 0.55f;
};

// Resolves the "ceiling + dynamic range" controls into absolute dB bounds.
// With autoCeiling the top follows the loudest recent band.
struct LevelRange {
    float floorDb;
    float ceilDb;
};
LevelRange computeRange(float rangeDb, float ceilDb, bool autoCeiling, float autoCeilingDb);

// Builds the 1024-entry RGB lookup (colormap + gamma + dB->colour).
void buildColorLut(const ViewParams& vp, uint32_t* lut1024);

// Renders the spectrogram into dst (0xAARRGGBB, top-down, stride in uint32).
// Region outside available history is filled with the darkest colour.
void renderSpectrogram(uint32_t* dst, int width, int height, size_t stride,
                       const FrameStore& frames, const MelBank& bank, const ViewParams& vp);

// Lookups for the cursor readout and the grid.
float frequencyAtY(float y, float height, const MelBank& bank, int axis, float fmin, float fmax);

// Vertical zoom: scale the displayed [fmin, fmax] window by actor (0.5 = zoom
// in) while keeping the frequency at pointerPos01 (0 = bottom, 1 = top) fixed on
// screen. Works in the axis's own coordinate so log/mel/linear all feel even.
struct FrequencyRange {
    float fmin;
    float fmax;
};
FrequencyRange zoomFrequencyRange(float fmin, float fmax, int axis, float pointerPos01, float factor,
                                 float limitLo, float limitHi);
float yForFrequency(float hz, float height, const MelBank& bank, int axis, float fmin, float fmax);

// Equal-tempered pitch to Hz (69 = A4 = 440 Hz).
float midiNoteToHz(float note);

// Secondary trace ink for a map: the map's own peak ink rotated in hue, so the reference
// input is distinguishable from the primary one without a second colour table.
void secondaryTraceInk(int cmap, int light, int choice, unsigned char out[3]);

// The gradient the reference input is drawn with. It is never the primary's: the map whose
// mean colour is furthest from it wins, so the two layers read as different data without
// the user having to choose a second colormap.
int referenceCmap(int primary);

// is this colormap offered in the given theme? (dark = true/false)
bool mapFitsTheme(int cmap, bool darkTheme);

// Value (dB) and position of one rendered pixel, using the same mapping as
// renderSpectrogram(). Returns -200 when the pixel holds no data.
float valueAtPixel(int x, int y, int width, int height, const FrameStore& frames, const MelBank& bank,
                   const ViewParams& vp, double* timeOut, float* hzOut);

// Blends the given pixel rows toward their inverse colour, weighted per row by
// `strengths` (0..255). Inverting keeps the line visible over both the black
// background and the brightest parts of the spectrogram, so it never disappears
// and never hides the data.
// Deterministic paper-like grain, stable per pixel so it reads as texture rather
// than video noise. amount 0..1.
void addGrain(uint32_t* dst, int width, int height, size_t stride, float amount);

void addGuideLines(uint32_t* dst, int width, int height, size_t stride, const int* rows,
                   const int* strengths, int count);

} // namespace mel

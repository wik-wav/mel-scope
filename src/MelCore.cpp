// MelCore.cpp -- mel-spectrogram analysis and pixel rendering.
#include "MelCore.h"

#include "Colormaps.h"

#include <algorithm>
#include <cstdio>

namespace mel {

// ---------------------------------------------------------------- parameters
namespace {

std::vector<ParamDef> makeParamDefs()
{
    std::vector<ParamDef> d;
    d.reserve(pCount);

    auto add = [&d](ParamDef p) { d.push_back(std::move(p)); };

    add({ "fft", "FFT size", "Analysis",
          "Window length: larger = sharper in frequency, smeared in time",
          0, 3, 2, true, false, "", { "2048", "4096", "8192", "16384" } });
    add({ "overlap", "Overlap", "Analysis",
          "Frame overlap: larger = more columns per second (smoother time axis)",
          0, 3, 2, true, false, "", { "50%", "75%", "87.5%", "93.75%" } });
    add({ "bands", "Mel bands", "Analysis",
          "Vertical resolution of the mel filterbank",
          0, 8, 3, true, false, "", { "64", "96", "128", "192", "256", "384", "512", "768", "1024" } });
    add({ "window", "Window", "Analysis",
          "Analysis window shape (Blackman-Harris has the lowest side lobes)",
          0, 3, 0, true, false, "", { "Hann", "Blackman-Harris", "Hamming", "Rectangular" } });
    add({ "channel", "Input", "Analysis",
          "Which channels feed the analyser",
          0, 2, 0, true, false, "", { "Mix", "Left", "Right" } });
    add({ "fmin", "Low cut", "Range",
          "Lowest frequency shown", 10.0f, 4000.0f, 30.0f, false, true, " Hz", {} });
    add({ "fmax", "High cut", "Range",
          "Highest frequency shown (clamped to Nyquist)", 500.0f, 24000.0f, 1500.0f, false, true, " Hz", {} });
    add({ "range", "Spectrum range", "Range",
          "Dynamic range of the spectrum image in dB below the ceiling", 15.0f, 120.0f, 120.0f, false,
          false, " dB", {} });
    add({ "ceil", "Ceiling", "Range",
          "Top of the colour scale in dB, used when Auto level is off (0 dB = full-scale sine)",
          -60.0f, 6.0f, 6.0f, false, false, " dB", {} });
    add({ "autoceil", "Auto level", "Range",
          "Track the loudest band and move the ceiling with it", 0, 1, 0, true, false, "", { "Off", "On" } });
    add({ "span", "Time span", "View",
          "Seconds of history across the width", 0.3f, 60.0f, 9.15f, false, true, " s", {} });
    add({ "mode", "Display", "View",
          "Harmonics traces spectral peaks into thin lines over a dimmed spectrum; Spectrum draws the mel image",
          0, 1, 1, true, false, "", { "Spectrum", "Harmonics" } });
    add({ "theme", "Theme", "View",
          "Dark or light interface (switching also points the canvas the same way)",
          0, 1, 0, true, false, "", { "Light", "Dark" } });
    add({ "axis", "Frequency axis", "View",
          "Mel lays every mel band on an equal row; Log/Linear remap the rows",
          0, 2, 1, true, false, "", { "Mel", "Log", "Linear" } });
    add({ "cmap", "Colormap", "View",
          "Bloom and Ink are the ink-on-paper maps; Ember/Ice are the dark canvases", 0, kNumMaps - 1, 7, true, false, "", {} });

    add({ "gamma", "Gamma", "View",
          "Mid-tone brightness of the colour map", 0.25f, 4.0f, 0.96f, false, false, "", {} });
    add({ "tilt", "Tilt", "View",
          "Spectral tilt in dB/octave around 1 kHz (negative = brighter highs)",
          -12.0f, 12.0f, -1.9f, false, false, " dB/oct", {} });
    add({ "glow", "Glow", "View",
          "Soft bloom over the image (0 = crisp)", 0.0f, 1.0f, 0.0f, false, false, "", {} });
    add({ "grain", "Grain", "View",
          "Paper texture: fine stable noise over the image", 0.0f, 1.0f, 0.20f, false, false, "", {} });    add({ "smooth", "Smoothing", "View",
          "Temporal smoothing between frames", 0.0f, 0.95f, 0.06f, false, false, "", {} });
    add({ "keys", "Key guides", "View",
          "Piano-key centre lines: blended toward the inverse colour so they stay visible over any level",
          0.0f, 1.0f, 0.13f, false, false, "", {} });
    add({ "freeze", "Freeze", "View",
          "Stop scrolling and hold the current image", 0, 1, 0, true, false, "", { "Off", "On" } });
    add({ "ui", "UI scale", "View",
          "Size of the interface text and controls, in percent of the host's scale. The window "
          "opens at this size too",
          75, 200, 115, true, false, " %", {} });
    add({ "line", "Peak range", "Range",
          "How many dB of level the peak traces span, counted down from the ceiling. Set apart "
          "from Spectrum range: keep that wide to see the spectrum and this narrower so quiet "
          "partials still read as lighter lines",
          10.0f, 120.0f, 60.0f, true, false, " dB", {} });
    add({ "notes", "MIDI notes", "View",
          "How strongly notes received on the MIDI input are drawn behind the spectrum, 0 = "
          "off. Each note is a band one semitone tall at its own pitch, so it lines up with "
          "what the analyser shows",
          0.0f, 100.0f, 55.0f, true, false, " %", {} });
    add({ "refon", "Reference input", "View",
          "Analyse the second input (the sidechain bus) and draw its traces over the "
          "primary ones. Feed it from another track to compare against a reference such as "
          "a solo take",
          0, 1, 1, true, false, "", { "Off", "On" } });
    add({ "reflevel", "Reference level", "View",
          "How strongly the reference traces are drawn", 0.0f, 100.0f, 80.0f, true, false,
          " %", {} });
    add({ "refink", "Reference colour", "View",
          "Colour of the reference traces. Auto derives it from the current colormap so it "
          "never collides with the primary ink; the fixed choices read on both themes",
          0, 4, 0, true, false, "", { "Auto", "Cyan", "Amber", "Magenta", "Lime" } });

    // choices for the colormap come from the generated table
    ParamDef& cmap = d[pCmap];
    for (int i = 0; i < kNumMaps; ++i) cmap.choices.push_back(kMapNames[i]);
    return d;
}

} // namespace

const std::vector<ParamDef>& paramDefs()
{
    static const std::vector<ParamDef> defs = makeParamDefs();
    return defs;
}

bool paramTableIsConsistent()
{
    const auto& d = paramDefs();
    if ((int)d.size() != (int)pCount) return false;
    struct Expect { const char* key; int id; };
    // Every parameter, in id order: a partial list is how two adjacent ids got their
    // keys swapped without any check noticing.
    static const Expect kExpect[] = {
        { "fft", pFft },           { "overlap", pOverlap }, { "bands", pBands },
        { "window", pWindow },     { "channel", pChannel }, { "fmin", pFmin },
        { "fmax", pFmax },         { "range", pRange },     { "ceil", pCeil },
        { "autoceil", pAutoCeil }, { "span", pSpan },       { "mode", pMode },
        { "theme", pTheme },       { "axis", pAxis },       { "cmap", pCmap },
        { "gamma", pGamma },       { "tilt", pTilt },       { "glow", pGlow },
        { "grain", pGrain },       { "smooth", pSmooth },   { "keys", pKeyLines },
        { "freeze", pFreeze },     { "ui", pUiScale },      { "line", pLineRange },
        { "notes", pNotes },     { "refon", pRefOn },       { "reflevel", pRefLevel },
        { "refink", pRefInk },
    };
    static_assert(sizeof(kExpect) / sizeof(kExpect[0]) == (size_t)pCount,
                  "the expected-key table must list every parameter");
    for (const auto& e : kExpect)
        if (paramIndexForKey(e.key) != e.id) return false;
    return true;
}

int paramIndexForKey(const char* key)
{
    const auto& d = paramDefs();
    for (size_t i = 0; i < d.size(); ++i)
        if (std::strcmp(d[i].key, key) == 0) return (int)i;
    return -1;
}

const int* fftSizeTable(int& count)
{
    // Below 2048 the bin width (23 Hz at 48 kHz) is wider than the mel bands in the
    // low range, so several bands share one bin and the image breaks into horizontal
    // bars. 32768 buys nothing under the 1.5 kHz default cut, so neither is offered.
    static const int t[] = { 2048, 4096, 8192, 16384 };
    count = 4;
    return t;
}

const int* bandsTable(int& count)
{
    static const int t[] = { 64, 96, 128, 192, 256, 384, 512, 768, 1024 };
    count = 9;
    return t;
}

const float* overlapTable(int& count)
{
    static const float t[] = { 0.5f, 0.75f, 0.875f, 0.9375f };
    count = 4;
    return t;
}

const char* windowName(int i)
{
    static const char* n[] = { "Hann", "Blackman-Harris", "Hamming", "Rectangular" };
    return n[std::max(0, std::min(3, i))];
}

int windowCount() { return 4; }

// ------------------------------------------------------------------ utilities
double hzToMelSlaney(double hz)
{
    const double fSp = 200.0 / 3.0;
    const double minLogHz = 1000.0, minLogMel = 15.0;
    const double logstep = std::log(6.4) / 27.0;
    if (hz < minLogHz) return hz / fSp;
    return minLogMel + std::log(hz / minLogHz) / logstep;
}

double melToHzSlaney(double mel)
{
    const double fSp = 200.0 / 3.0;
    const double minLogHz = 1000.0, minLogMel = 15.0;
    const double logstep = std::log(6.4) / 27.0;
    if (mel < minLogMel) return mel * fSp;
    return minLogHz * std::exp(logstep * (mel - minLogMel));
}

// ------------------------------------------------------------------------ FFT
void Fft::setup(int n)
{
    n_ = n;
    log2n_ = 0;
    while ((1 << log2n_) < n) ++log2n_;
    re_.assign((size_t)n, 0.0f);
    im_.assign((size_t)n, 0.0f);
    twCos_.resize((size_t)(n / 2));
    twSin_.resize((size_t)(n / 2));
    for (int i = 0; i < n / 2; ++i) {
        const double a = -2.0 * 3.14159265358979323846 * i / n;
        twCos_[(size_t)i] = (float)std::cos(a);
        twSin_[(size_t)i] = (float)std::sin(a);
    }
    bitrev_.resize((size_t)n);
    for (int i = 0; i < n; ++i) {
        int r = 0;
        for (int b = 0; b < log2n_; ++b)
            if (i & (1 << b)) r |= 1 << (log2n_ - 1 - b);
        bitrev_[(size_t)i] = r;
    }
}

void Fft::magnitudes(const float* in, const float* window, float* out) const
{
    const int n = n_;
    double winSum = 0.0;
    for (int i = 0; i < n; ++i) {
        const float w = window[i];
        winSum += w;
        const int j = bitrev_[(size_t)i];
        re_[(size_t)j] = in[i] * w;
        im_[(size_t)j] = 0.0f;
    }
    // iterative radix-2 decimation in time
    for (int size = 2; size <= n; size <<= 1) {
        const int half = size >> 1;
        const int step = n / size;
        for (int base = 0; base < n; base += size) {
            for (int k = 0; k < half; ++k) {
                const float wr = twCos_[(size_t)(k * step)];
                const float wi = twSin_[(size_t)(k * step)];
                const int a = base + k, b = a + half;
                const float xr = re_[(size_t)b] * wr - im_[(size_t)b] * wi;
                const float xi = re_[(size_t)b] * wi + im_[(size_t)b] * wr;
                re_[(size_t)b] = re_[(size_t)a] - xr;
                im_[(size_t)b] = im_[(size_t)a] - xi;
                re_[(size_t)a] += xr;
                im_[(size_t)a] += xi;
            }
        }
    }
    const float scale = winSum > 0.0 ? (float)(2.0 / winSum) : 0.0f;
    for (int k = 0; k <= n / 2; ++k) {
        const float re = re_[(size_t)k], im = im_[(size_t)k];
        out[k] = std::sqrt(re * re + im * im) * scale;
    }
}

void makeWindow(int kind, int n, std::vector<float>& out)
{
    out.resize((size_t)n);
    const double denom = (double)(n - 1);
    for (int i = 0; i < n; ++i) {
        const double x = (double)i / denom * 2.0 * 3.14159265358979323846;
        double w = 1.0;
        switch (kind) {
        case 0: w = 0.5 - 0.5 * std::cos(x); break;                       // Hann
        case 1: w = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x)
                    - 0.01168 * std::cos(3 * x); break;                  // Blackman-Harris
        case 2: w = 0.54 - 0.46 * std::cos(x); break;                     // Hamming
        default: w = 1.0; break;
        }
        out[(size_t)i] = (float)w;
    }
}

void magnitudesToPower(float* mags, int count)
{
    for (int i = 0; i < count; ++i) mags[i] *= mags[i];
}

int AnalysisConfig::hopSize() const
{
    int n = 4;
    const float* o = overlapTable(n);
    const int i = std::max(0, std::min(n - 1, overlapIndex));
    return std::max(1, (int)std::lround(fftSize * (1.0 - (double)o[(size_t)i])));
}

bool AnalysisConfig::sameAnalysis(const AnalysisConfig& o) const
{
    return fftSize == o.fftSize && overlapIndex == o.overlapIndex && bands == o.bands
           && window == o.window && channel == o.channel && std::fabs(fmin - o.fmin) < 0.01f
           && std::fabs(fmax - o.fmax) < 0.01f && std::fabs(smooth - o.smooth) < 1e-4f
           && std::fabs(sampleRate - o.sampleRate) < 1e-6;
}

// ------------------------------------------------------------ mel filterbank
void MelBank::build(int bands_, int fftSize_, double sampleRate_, float fmin_, float fmax_)
{    bands = std::max(8, bands_);
    fftSize = fftSize_;
    sampleRate = sampleRate_;
    const float nyquist = (float)(sampleRate_ * 0.5);
    fmax = std::min(fmax_, nyquist);
    fmin = std::max(10.0f, std::min(fmin_, fmax * 0.85f));

    const int nbins = fftSize / 2 + 1;
    const double mlo = hzToMelSlaney(fmin), mhi = hzToMelSlaney(fmax);
    std::vector<double> edges((size_t)bands + 2);
    for (int i = 0; i < bands + 2; ++i) {
        const double t = (double)i / (double)(bands + 1);
        edges[(size_t)i] = melToHzSlaney(mlo + (mhi - mlo) * t);
    }

    centerHz.resize((size_t)bands);
    for (int b = 0; b < bands; ++b) centerHz[(size_t)b] = (float)edges[(size_t)b + 1];

    offset.assign((size_t)bands + 1, 0);
    binStart.assign((size_t)bands, 0);
    weight.clear();
    weight.reserve((size_t)bands * 4);

    const double binHz = sampleRate_ / (double)fftSize;
    for (int b = 0; b < bands; ++b) {
        const double left = edges[(size_t)b], centre = edges[(size_t)b + 1], right = edges[(size_t)b + 2];
        int k0 = (int)std::ceil(left / binHz);
        int k1 = (int)std::floor(right / binHz);
        k0 = std::max(k0, 0);
        k1 = std::min(k1, nbins - 1);
        const double areaNorm = 1.0; // plain sum: keeps the mel dB scale equal to the peak dB scale
        if (k0 > k1) {
            // The band is narrower than one bin (low frequencies with a short
            // window). Snapping to the nearest bin would give this band exactly the
            // same value as its neighbours and draw flat horizontal blocks, so the
            // band centre is interpolated across the two surrounding bins instead.
            const double pos = centre / binHz;
            int k = (int)std::floor(pos);
            k = std::max(0, std::min(nbins - 2, k));
            const double frac = std::max(0.0, std::min(1.0, pos - (double)k));
            binStart[(size_t)b] = k;
            weight.push_back((float)((1.0 - frac) * areaNorm));
            weight.push_back((float)(frac * areaNorm));
        } else {
            binStart[(size_t)b] = k0;
            for (int k = k0; k <= k1; ++k) {
                const double f = k * binHz;
                double w;
                if (f <= centre) w = (f - left) / std::max(1e-9, centre - left);
                else w = (right - f) / std::max(1e-9, right - centre);
                if (w < 0.0) w = 0.0;
                weight.push_back((float)(w * areaNorm));
            }
        }
        offset[(size_t)b + 1] = (int)weight.size();
    }
}

void MelBank::apply(const float* power, float* out) const
{
    const float* w = weight.data();
    for (int b = 0; b < bands; ++b) {
        const int a = offset[(size_t)b], e = offset[(size_t)b + 1];
        const int base = binStart[(size_t)b];
        float acc = 0.0f;
        for (int i = a; i < e; ++i) acc += w[i] * power[base + (i - a)];
        out[b] = acc;
    }
}

float MelBank::bandToHz(float pos) const
{
    if (bands <= 0) return 0.0f;
    pos = clampf(pos, 0.0f, (float)(bands - 1));
    const int i0 = (int)pos;
    const int i1 = std::min(i0 + 1, bands - 1);
    const float f = pos - (float)i0;
    return centerHz[(size_t)i0] * (1.0f - f) + centerHz[(size_t)i1] * f;
}

float MelBank::hzToBand(float hz) const
{
    if (bands <= 0 || fmax <= fmin) return 0.0f;
    const double mlo = hzToMelSlaney(fmin), mhi = hzToMelSlaney(fmax);
    const double m = hzToMelSlaney(std::max((double)fmin, (double)hz));
    return (float)((m - mlo) / std::max(1e-9, mhi - mlo) * (bands - 1));
}

// ---------------------------------------------------------------- frame store
void FrameStore::init(int bands_, int capacity)
{
    bands = std::max(1, bands_);
    cap = std::max(16, capacity);
    db.assign((size_t)cap * (size_t)bands, -200.0f);
    times.assign((size_t)cap, 0.0);
    peaks.assign((size_t)cap * (size_t)kMaxPeaks, Peak{ 0.0f, -200.0f });
    peakCount.assign((size_t)cap, 0);
    head = 0;
    count = 0;
}

void FrameStore::clear()
{
    std::fill(db.begin(), db.end(), -200.0f);
    std::fill(times.begin(), times.end(), 0.0);
    std::fill(peaks.begin(), peaks.end(), Peak{ 0.0f, -200.0f });
    std::fill(peakCount.begin(), peakCount.end(), (uint16_t)0);
    head = 0;
    count = 0;
}

void FrameStore::push(const float* values, double time, const Peak* pks, int nPeaks)
{
    const int s = head;
    std::memcpy(&db[(size_t)s * bands], values, sizeof(float) * (size_t)bands);
    times[(size_t)s] = time;
    const int n = std::max(0, std::min(nPeaks, kMaxPeaks));
    if (n > 0 && pks) std::memcpy(&peaks[(size_t)s * kMaxPeaks], pks, sizeof(Peak) * (size_t)n);
    peakCount[(size_t)s] = (uint16_t)n;
    head = (head + 1) % cap;
    if (count < cap) ++count;
}

int FrameStore::lowerBound(double t) const
{
    if (count <= 0) return 0;
    if (t <= timeAt(0)) return 0;
    if (t >= timeAt(count - 1)) return count - 1;
    int lo = 0, hi = count - 1;
    while (lo + 1 < hi) {
        const int mid = (lo + hi) / 2;
        if (timeAt(mid) <= t) lo = mid; else hi = mid;
    }
    return lo;
}

// ------------------------------------------------------------------ analyser
namespace {
constexpr int kRingSize = 1 << 17;
constexpr int kRingMask = kRingSize - 1;
// Frames per second scale with the overlap, so a fixed frame count means a very
// different amount of history: 2048 frames is 43 s at 87.5 % overlap but only 5.5 s
// at 93.75 %, which made the Time span setting do nothing above that. Size the ring
// from the hop instead, aiming at kHistorySeconds and staying inside a memory cap.
constexpr double kHistorySeconds = 30.0;
constexpr int kMinFrames = 1024;
constexpr int kMaxFrames = 6144;

int frameCapacityFor(const AnalysisConfig& cfg)
{
    const double fps = (double)cfg.sampleRate / (double)std::max(1, cfg.hopSize());
    const int want = (int)std::ceil(kHistorySeconds * fps);
    return std::max(kMinFrames, std::min(kMaxFrames, want));
}
} // namespace

void Analyzer::configure(const AnalysisConfig& cfg)
{
    cfg_ = cfg;
    if (cfg_.sampleRate <= 0) cfg_.sampleRate = 48000.0;
    bank_.build(cfg_.bands, cfg_.fftSize, cfg_.sampleRate, cfg_.fmin, cfg_.fmax);
    cfg_.fmin = bank_.fmin;
    cfg_.fmax = bank_.fmax;
    hop_ = cfg_.hopSize();
    // Never assume the FFT was set up before: a first configuration that happens
    // to equal the defaults must still build the tables.
    if (fft_.size() != cfg_.fftSize) fft_.setup(cfg_.fftSize);
    if (windowKind_ != cfg_.window || (int)window_.size() != cfg_.fftSize) {
        makeWindow(cfg_.window, cfg_.fftSize, window_);
        windowKind_ = cfg_.window;
    }
    hist_.assign((size_t)cfg_.fftSize, 0.0f);
    buf_.assign((size_t)cfg_.fftSize, 0.0f);
    mags_.assign((size_t)(cfg_.fftSize / 2 + 1), 0.0f);
    smooth_.assign((size_t)bank_.bands, -200.0f);
    values_.assign((size_t)bank_.bands, -200.0f);
    histPos_ = 0;
    sinceFrame_ = 0;
    samplesSeen_ = 0;
    peakDb_ = -20.0f;
}

void Analyzer::reset()
{
    std::fill(hist_.begin(), hist_.end(), 0.0f);
    std::fill(smooth_.begin(), smooth_.end(), -200.0f);
    histPos_ = 0;
    sinceFrame_ = 0;
    samplesSeen_ = 0;
    peakDb_ = -20.0f;
}

void Analyzer::process(const float* mono, int numSamples, FrameStore& sink)
{
    if (cfg_.fftSize <= 0 || bank_.bands <= 0) return;
    for (int i = 0; i < numSamples; ++i) {
        hist_[(size_t)histPos_] = mono[i];
        histPos_ = (histPos_ + 1) % cfg_.fftSize;
        ++sinceFrame_;
        ++samplesSeen_;
        if (sinceFrame_ < hop_) continue;
        sinceFrame_ -= hop_;
        emitFrame(sink);
    }
}

void Analyzer::emitFrame(FrameStore& sink)
{
    const int n = cfg_.fftSize;
    const int bands = bank_.bands;
    for (int k = 0; k < n; ++k)
        buf_[(size_t)k] = hist_[(size_t)((histPos_ - n + k + n) % n)];

    fft_.magnitudes(buf_.data(), window_.data(), mags_.data());

    // ---- traced spectral peaks, on the linear-frequency magnitude spectrum
    const int nbins = n / 2 + 1;
    specDb_.resize((size_t)nbins);
    float maxDb = -400.0f;
    for (int k = 0; k < nbins; ++k) {
        const float d = 20.0f * std::log10(std::max(1e-9f, mags_[(size_t)k]));
        specDb_[(size_t)k] = d;
        if (d > maxDb) maxDb = d;
    }
    peaks_.clear();
    const double binHz = cfg_.sampleRate / (double)n;
    const float keepAbove = maxDb - 30.0f; // peaks this far under the loudest are dropped
    for (int k = 1; k < nbins - 1 && (int)peaks_.size() < kMaxPeaks; ++k) {
        const float a = specDb_[(size_t)k - 1], b = specDb_[(size_t)k], c = specDb_[(size_t)k + 1];
        if (!(b > a && b >= c)) continue;
        const float den = a - 2.0f * b + c;
        float delta = 0.0f;
        if (std::fabs(den) > 1e-9f) delta = 0.5f * (a - c) / den;
        delta = clampf(delta, -0.5f, 0.5f);
        const float amp = b - 0.25f * (a - c) * delta;
        if (amp < keepAbove) continue;
        const double hz = ((double)k + (double)delta) * binHz;
        if (hz < 20.0) continue;
        peaks_.push_back(Peak{ (float)hz, amp });
    }

    magnitudesToPower(mags_.data(), nbins);
    bank_.apply(mags_.data(), values_.data());

    const float a = clampf(cfg_.smooth, 0.0f, 0.98f);
    float mx = -200.0f;
    for (int b = 0; b < bands; ++b) {
        float p = values_[(size_t)b];
        if (!(p > 1e-13f)) p = 1e-13f;
        const float db = 10.0f * std::log10(p);
        const float v = a > 0.0f ? (a * smooth_[(size_t)b] + (1.0f - a) * db) : db;
        smooth_[(size_t)b] = v;
        values_[(size_t)b] = v;
        if (v > mx) mx = v;
    }

    const double dt = (double)hop_ / std::max(1.0, cfg_.sampleRate);
    peakDb_ = std::max(mx, peakDb_ - (float)(8.0 * dt)); // 8 dB/s decay
    if (peakDb_ < -110.0f) peakDb_ = -110.0f;

    const double t = (double)samplesSeen_ / std::max(1.0, cfg_.sampleRate);
    sink.push(values_.data(), t, peaks_.data(), (int)peaks_.size());
}

// --------------------------------------------------------------------- engine
Engine::Engine()
{
    ring_.assign(kRingSize, 0.0f);
    store_.init(192, 2048);
}

Engine::~Engine() { stop(); }

void Engine::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0 ? sampleRate : 48000.0;
    AnalysisConfig c = cfg_;
    c.sampleRate = sampleRate_;
    setAnalysisConfig(c);
}

void Engine::start()
{
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { threadMain(); });
}

void Engine::stop()
{
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

int Engine::ringRead(float* dst, int maxCount)
{
    const int r = readPos_.load(std::memory_order_relaxed);
    const int w = writePos_.load(std::memory_order_acquire);
    const int avail = (w - r) & kRingMask;
    const int n = std::min(avail, maxCount);
    for (int i = 0; i < n; ++i) dst[i] = ring_[(size_t)((r + i) & kRingMask)];
    readPos_.store((r + n) & kRingMask, std::memory_order_release);
    return n;
}

void Engine::pushAudio(const float* const* channels, int numChannels, int numSamples)
{
    if (numChannels <= 0 || numSamples <= 0) return;
    int w = writePos_.load(std::memory_order_relaxed);
    const int r = readPos_.load(std::memory_order_acquire);
    const int space = (r - w - 1) & kRingMask;
    if (space <= 0) return;
    const int n = std::min(numSamples, space);

    // Mono is the common case (and the analyser is mono anyway): copy the
    // samples straight in with no per-sample arithmetic. Stereo is downmixed.
    const int ch = channel_.load(std::memory_order_relaxed);
    if (numChannels == 1 || ch == 1 || ch == 2) {
        const int src = (numChannels == 1) ? 0 : std::min(ch - 1, numChannels - 1);
        const float* src0 = channels[src];
        if (src0) {
            const int first = std::min(n, kRingSize - w);
            std::memcpy(&ring_[(size_t)w], src0, sizeof(float) * (size_t)first);
            if (n > first) std::memcpy(&ring_[0], src0 + first, sizeof(float) * (size_t)(n - first));
        }
    } else {
        const float gain = 1.0f / (float)numChannels;
        for (int i = 0; i < n; ++i) {
            float s = 0.0f;
            for (int c = 0; c < numChannels; ++c) s += channels[c][i];
            ring_[(size_t)((w + i) & kRingMask)] = s * gain;
        }
    }
    writePos_.store((w + n) & kRingMask, std::memory_order_release);
}

void Engine::setAnalysisConfig(const AnalysisConfig& cfg)
{
    {
        std::lock_guard<std::mutex> lk(pendingMu_);
        pending_ = cfg;
    }
    channel_.store(cfg.channel, std::memory_order_relaxed);
    pendingEpoch_.fetch_add(1, std::memory_order_release);
    if (!running_.load()) {
        std::lock_guard<std::mutex> lk(pendingMu_);
        reconfigure(pending_);
        appliedEpoch_ = pendingEpoch_.load(std::memory_order_acquire);
    }
}

void Engine::setFrozen(bool f) { frozen_.store(f, std::memory_order_relaxed); }

void Engine::clearHistory()
{
    std::lock_guard<std::mutex> lk(mu_);
    store_.clear();
    latestTime_.store(0.0);
}

void Engine::reconfigure(const AnalysisConfig& cfg)
{
    cfg_ = cfg;
    channel_.store(cfg_.channel, std::memory_order_relaxed);
    if (cfg_.sampleRate <= 0) cfg_.sampleRate = sampleRate_;
    bank_.build(cfg_.bands, cfg_.fftSize, cfg_.sampleRate, cfg_.fmin, cfg_.fmax);
    cfg_.fmin = bank_.fmin;
    cfg_.fmax = bank_.fmax;
    std::lock_guard<std::mutex> lk(mu_);
    store_.init(bank_.bands, frameCapacityFor(cfg_));
}

void Engine::threadMain()
{
    Analyzer analyzer;
    std::vector<float> pull(4096);
    bool needConfigure = true;

    while (running_.load(std::memory_order_acquire)) {
        const uint32_t epoch = pendingEpoch_.load(std::memory_order_acquire);
        if (needConfigure || epoch != appliedEpoch_) {
            AnalysisConfig c;
            {
                std::lock_guard<std::mutex> lk(pendingMu_);
                c = pending_;
            }
            if (c.sampleRate <= 0) c.sampleRate = sampleRate_;
            analyzer.configure(c);
            appliedEpoch_ = epoch;
            needConfigure = false;
            std::lock_guard<std::mutex> lk(mu_);
            cfg_ = analyzer.config();
            channel_.store(cfg_.channel, std::memory_order_relaxed);
            bank_ = analyzer.bank();
            store_.init(bank_.bands, frameCapacityFor(cfg_));
        }

        if (frozen_.load(std::memory_order_relaxed)) {
            // Frozen: drain the incoming audio so the ring does not overflow, but do
            // not analyse it - the held frames must not be overwritten.
            ringRead(pull.data(), (int)pull.size());
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }

        const int got = ringRead(pull.data(), (int)pull.size());
        if (got <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        {
            std::lock_guard<std::mutex> lk(mu_);
            analyzer.process(pull.data(), got, store_);
            autoCeil_.store(std::min(0.0f, analyzer.peakDb() + 2.0f));
            if (store_.count > 0) latestTime_.store(store_.timeAt(store_.count - 1));
        }
    }
}

// -------------------------------------------------------------- colour lookups
LevelRange computeRange(float rangeDb, float ceilDb, bool autoCeiling, float autoCeilingDb)
{
    LevelRange r;
    // Do not let the automatic ceiling collapse into the noise: after a long
    // pause it decays towards -110 dB, and a floor 72 dB under that would put
    // digital silence (-130 dB) half way up the ramp.
    r.ceilDb = autoCeiling ? clampf(autoCeilingDb, -80.0f, 0.0f) : ceilDb;
    r.floorDb = r.ceilDb - std::max(6.0f, rangeDb);
    // Absolute floor: anything below this is inaudible and belongs on the canvas.
    r.floorDb = std::max(r.floorDb, -108.0f);
    return r;
}

void buildColorLut(const ViewParams& vp, uint32_t* lut1024)
{
    const int map = std::max(0, std::min(kNumMaps - 1, vp.cmap));
    const unsigned char(*m)[3] = kMaps[map];
    // Light canvas: traverse the ramp backwards so silence is the pale end and
    // loud content is ink. Maps that are already light (Paper, Sand) are left
    // alone, since reversing them would put the image back on a dark ground.
    const bool reverse = (vp.light != 0) && (kMapIsLight[map] == 0);
    const float g = std::max(0.05f, vp.gamma);
    for (int i = 0; i < 1024; ++i) {
        const float n = (float)i / 1023.0f;
        const float s = std::pow(n, g);
        int idx = (int)(s * 255.0f + 0.5f);
        if (idx < 0) idx = 0;
        if (idx > 255) idx = 255;
        if (reverse) idx = 255 - idx;
        lut1024[i] = 0xFF000000u | ((uint32_t)m[idx][0] << 16) | ((uint32_t)m[idx][1] << 8)
                     | (uint32_t)m[idx][2];
    }
}

// The vertical axis is a warp over the VIEW window: row position p (0 = bottom)
// maps to a frequency, and that frequency maps to a position in the mel bank.
// Everything (wash, peaks, labels, readout) goes through these two, so a zoom can
// never leave the image and the traces disagreeing.
static float hzAtPosition(float p, int axis, float fmin, float fmax)
{
    p = clampf(p, 0.0f, 1.0f);
    if (!(fmax > fmin) || fmin <= 0.0f) return fmin;
    if (axis == 0) { // mel
        const double mlo = hzToMelSlaney(fmin), mhi = hzToMelSlaney(fmax);
        return (float)melToHzSlaney(mlo + (mhi - mlo) * p);
    }
    if (axis == 1) return fmin * (float)std::pow((double)fmax / (double)fmin, (double)p);
    return fmin + (fmax - fmin) * p;
}

static float positionForHz(float hz, int axis, float fmin, float fmax)
{
    if (!(fmax > fmin) || hz <= 0.0f) return 0.0f;
    if (axis == 0) {
        const double mlo = hzToMelSlaney(fmin), mhi = hzToMelSlaney(fmax);
        return (float)((hzToMelSlaney(hz) - mlo) / std::max(1e-9, mhi - mlo));
    }
    if (axis == 1)
        return (float)(std::log((double)hz / fmin) / std::log((double)fmax / (double)fmin));
    return (hz - fmin) / (fmax - fmin);
}

// row position -> position in the mel bank (0 .. bands-1)
static float bandPosAtRow(float p, int axis, float fmin, float fmax, const MelBank& bank)
{
    if (bank.bands <= 0) return 0.0f;
    const float hz = hzAtPosition(p, axis, fmin, fmax);
    return clampf(bank.hzToBand(hz), 0.0f, (float)(bank.bands - 1));
}

bool mapFitsTheme(int cmap, bool darkTheme)
{
    if (cmap < 0 || cmap >= kNumMaps) return false;
    const int tag = kMapTheme[cmap];
    return tag == 2 || tag == (darkTheme ? 1 : 0);
}

float frequencyAtY(float y, float height, const MelBank& bank, int axis, float fmin, float fmax)
{
    (void)bank;
    if (height <= 1.0f) return fmin;
    const float p = clampf((height - 1.0f - y) / (height - 1.0f), 0.0f, 1.0f);
    return hzAtPosition(p, axis, fmin, fmax);
}

float yForFrequency(float hz, float height, const MelBank& bank, int axis, float fmin, float fmax)
{
    (void)bank;
    if (height <= 1.0f || hz <= 0.0f) return 0.0f;
    const float p = positionForHz(hz, axis, fmin, fmax);
    return (height - 1.0f) * (1.0f - clampf(p, 0.0f, 1.0f));
}

float valueAtPixel(int x, int y, int width, int height, const FrameStore& frames, const MelBank& bank,
                   const ViewParams& vp, double* timeOut, float* hzOut)
{
    if (hzOut) *hzOut = frequencyAtY((float)y, (float)height, bank, vp.axis, vp.fmin, vp.fmax);
    if (frames.count <= 0 || bank.bands <= 0 || width < 2 || height < 2) {
        if (timeOut) *timeOut = vp.now;
        return -200.0f;
    }
    const double tq = vp.now - (double)vp.span * (double)(width - 1 - x) / (double)(width - 1);
    if (timeOut) *timeOut = tq;
    const int i0 = frames.lowerBound(tq);
    int i1 = std::min(i0 + 1, frames.count - 1);
    const double dt = frames.timeAt(i1) - frames.timeAt(i0);
    const float tf = dt > 1e-12 ? clampf((float)((tq - frames.timeAt(i0)) / dt), 0.0f, 1.0f) : 0.0f;

    const float p = clampf((float)(height - 1 - y) / (float)(height - 1), 0.0f, 1.0f);
    float pos = bandPosAtRow(p, vp.axis, vp.fmin, vp.fmax, bank);
    pos = clampf(pos, 0.0f, (float)(bank.bands - 1));
    const int b0 = (int)pos;
    const int b1 = std::min(b0 + 1, bank.bands - 1);
    const float bf = pos - (float)b0;

    const float* f0 = frames.frameAt(i0);
    const float* f1 = frames.frameAt(i1);
    const float v0 = f0[b0] * (1.0f - bf) + f0[b1] * bf;
    const float v1 = f1[b0] * (1.0f - bf) + f1[b1] * bf;
    const float oct = std::log2(std::max(1.0f, bank.centerHz[(size_t)std::min(bank.bands - 1, (int)(pos + 0.5f))]) / 1000.0f);
    return v0 * (1.0f - tf) + v1 * tf - vp.tilt * oct;
}

static void blurPass(uint32_t* src, uint32_t* tmp, int w, int h, int radius)
{
    // horizontal into tmp
    for (int y = 0; y < h; ++y) {
        const uint32_t* row = src + (size_t)y * w;
        uint32_t* out = tmp + (size_t)y * w;
        int rs = 0, gs = 0, bs = 0;
        const int win = radius * 2 + 1;
        for (int x = -radius; x <= radius; ++x) {
            const uint32_t c = row[std::max(0, std::min(w - 1, x))];
            rs += (int)((c >> 16) & 0xFF);
            gs += (int)((c >> 8) & 0xFF);
            bs += (int)(c & 0xFF);
        }
        for (int x = 0; x < w; ++x) {
            out[(size_t)x] = 0xFF000000u | ((uint32_t)(rs / win) << 16) | ((uint32_t)(gs / win) << 8)
                             | (uint32_t)(bs / win);
            const uint32_t add = row[std::min(w - 1, x + radius + 1)];
            const uint32_t sub = row[std::max(0, x - radius)];
            rs += (int)((add >> 16) & 0xFF) - (int)((sub >> 16) & 0xFF);
            gs += (int)((add >> 8) & 0xFF) - (int)((sub >> 8) & 0xFF);
            bs += (int)(add & 0xFF) - (int)(sub & 0xFF);
        }
    }
    // vertical back into src
    for (int x = 0; x < w; ++x) {
        int rs = 0, gs = 0, bs = 0;
        const int win = radius * 2 + 1;
        for (int y = -radius; y <= radius; ++y) {
            const uint32_t c = tmp[(size_t)std::max(0, std::min(h - 1, y)) * w + x];
            rs += (int)((c >> 16) & 0xFF);
            gs += (int)((c >> 8) & 0xFF);
            bs += (int)(c & 0xFF);
        }
        for (int y = 0; y < h; ++y) {
            src[(size_t)y * w + x] = 0xFF000000u | ((uint32_t)(rs / win) << 16)
                                     | ((uint32_t)(gs / win) << 8) | (uint32_t)(bs / win);
            const uint32_t add = tmp[(size_t)std::min(h - 1, y + radius + 1) * w + x];
            const uint32_t sub = tmp[(size_t)std::max(0, y - radius) * w + x];
            rs += (int)((add >> 16) & 0xFF) - (int)((sub >> 16) & 0xFF);
            gs += (int)((add >> 8) & 0xFF) - (int)((sub >> 8) & 0xFF);
            bs += (int)(add & 0xFF) - (int)(sub & 0xFF);
        }
    }
}

void blendMidiNotes(uint32_t* flat, int width, int height, size_t stride, uint32_t canvas,
                    const MelBank& bank, const ViewParams& vp);

static void renderSpectrum(uint32_t* dst, int width, int height, size_t stride,
                       const FrameStore& frames, const MelBank& bank, const ViewParams& vp)
{
    uint32_t lut[1024];
    buildColorLut(vp, lut);
    const uint32_t bg = lut[0];

    for (int y = 0; y < height; ++y) {
        uint32_t* row = dst + (size_t)y * stride;
        for (int x = 0; x < width; ++x) row[x] = bg;
    }
    if (width < 2 || height < 2 || frames.count <= 0 || bank.bands <= 0) return;

    const int B = bank.bands;
    const float range = std::max(1.0f, vp.ceilDb - vp.floorDb);
    const float invRange = 1.0f / range;
    const float bandsF = (float)B;

    static thread_local std::vector<float> tiltDb, yB0f, yB1f, yBfrac, yTilt;
    static thread_local std::vector<int> yB0, yB1;
    static thread_local std::vector<int> xI0, xI1;
    static thread_local std::vector<float> xFrac;
    static thread_local std::vector<char> xValid;
    static thread_local std::vector<uint32_t> blurTmp;

    tiltDb.resize((size_t)B);
    for (int b = 0; b < B; ++b) {
        const float oct = std::log2(std::max(1.0f, bank.centerHz[(size_t)b]) / 1000.0f);
        tiltDb[(size_t)b] = vp.tilt * oct;
    }

    yB0.resize((size_t)height); yB1.resize((size_t)height);
    yBfrac.resize((size_t)height); yTilt.resize((size_t)height);
    for (int y = 0; y < height; ++y) {
        const float p = (float)(height - 1 - y) / (float)(height - 1);
        float pos = bandPosAtRow(clampf(p, 0.0f, 1.0f), vp.axis, vp.fmin, vp.fmax, bank);
        pos = clampf(pos, 0.0f, bandsF - 1.0f);
        int b0 = (int)pos;
        if (b0 > B - 1) b0 = B - 1;
        const int b1 = std::min(b0 + 1, B - 1);
        const float f = pos - (float)b0;
        yB0[(size_t)y] = b0;
        yB1[(size_t)y] = b1;
        yBfrac[(size_t)y] = f;
        yTilt[(size_t)y] = tiltDb[(size_t)b0] * (1.0f - f) + tiltDb[(size_t)b1] * f;
    }

    xI0.resize((size_t)width); xI1.resize((size_t)width);
    xFrac.resize((size_t)width); xValid.resize((size_t)width);
    const double t0 = frames.timeAt(0);
    for (int x = 0; x < width; ++x) {
        const double tq = vp.now - (double)vp.span * (double)(width - 1 - x) / (double)(width - 1);
        if (tq < t0) { // before the oldest frame: no data at all
            xValid[(size_t)x] = 0;
            xI0[(size_t)x] = xI1[(size_t)x] = 0;
            xFrac[(size_t)x] = 0.0f;
            continue;
        }
        int i0 = frames.lowerBound(tq);
        int i1 = i0 + 1;
        if (i1 > frames.count - 1) i1 = frames.count - 1;
        double dt = frames.timeAt(i1) - frames.timeAt(i0);
        float frac = dt > 1e-12 ? (float)((tq - frames.timeAt(i0)) / dt) : 0.0f;
        xValid[(size_t)x] = 1;
        xI0[(size_t)x] = i0;
        xI1[(size_t)x] = i1;
        xFrac[(size_t)x] = clampf(frac, 0.0f, 1.0f);
    }

    const float floorDb = vp.floorDb;
    for (int y = 0; y < height; ++y) {
        uint32_t* row = dst + (size_t)y * stride;
        const int b0 = yB0[(size_t)y], b1 = yB1[(size_t)y];
        const float bf = yBfrac[(size_t)y];
        const float tilt = yTilt[(size_t)y];
        const float base = tilt - floorDb;
        for (int x = 0; x < width; ++x) {
            if (!xValid[(size_t)x]) continue;
            const float* f0 = frames.frameAt(xI0[(size_t)x]);
            const float* f1 = frames.frameAt(xI1[(size_t)x]);
            const float tf = xFrac[(size_t)x];
            const float v0 = f0[b0] * (1.0f - bf) + f0[b1] * bf;
            const float v1 = f1[b0] * (1.0f - bf) + f1[b1] * bf;
            float n = (v0 * (1.0f - tf) + v1 * tf + base) * invRange;
            if (!(n > 0.0f)) n = 0.0f;
            int idx = (int)(n * 1023.0f);
            if (idx > 1023) idx = 1023;
            row[x] = lut[idx];
        }
    }

    if (vp.glow > 0.001f) {
        const int radius = (int)std::lround(std::min(1.0f, vp.glow) * 10.0f);
        if (radius >= 1) {
            blurTmp.resize((size_t)width * (size_t)height);
            // blur works on a contiguous copy (stride may differ from width)
            static thread_local std::vector<uint32_t> flat;
            flat.resize((size_t)width * (size_t)height);
            for (int y = 0; y < height; ++y)
                std::memcpy(&flat[(size_t)y * width], dst + (size_t)y * stride, sizeof(uint32_t) * (size_t)width);
            blurPass(flat.data(), blurTmp.data(), width, height, radius);
            for (int y = 0; y < height; ++y)
                std::memcpy(dst + (size_t)y * stride, &flat[(size_t)y * width], sizeof(uint32_t) * (size_t)width);
        }
    }

    // played notes sit under the spectrum in this display too
    blendMidiNotes(dst, width, height, stride, bg, bank, vp);
}

void addGrain(uint32_t* dst, int width, int height, size_t stride, float amount)
{
    const float a = clampf(amount, 0.0f, 1.0f);
    if (a <= 0.001f) return;
    const int amp = (int)std::lround(a * 12.0f);
    if (amp <= 0) return;
    for (int y = 0; y < height; ++y) {
        uint32_t* row = dst + (size_t)y * stride;
        for (int x = 0; x < width; ++x) {
            uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
            h ^= h >> 13;
            h *= 1274126177u;
            h ^= h >> 16;
            const int n = (int)((h >> 24) & 0xFF) - 128; // -128..127
            const int d = (n * amp) >> 7;
            const uint32_t c = row[x];
            int r = (int)((c >> 16) & 0xFF) + d;
            int g = (int)((c >> 8) & 0xFF) + d;
            int b = (int)(c & 0xFF) + d;
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            row[x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}

void addGuideLines(uint32_t* dst, int width, int height, size_t stride, const int* rows,
                   const int* strengths, int count)
{
    // out = mix(c, 255 - c, strength): a difference blend. Over near-black it
    // lightens, over the hot end of the colour map it darkens, so the guide is
    // always visible without covering anything up.
    for (int i = 0; i < count; ++i) {
        const int y = rows[i];
        if (y < 0 || y >= height) continue;
        const int a = strengths ? std::max(0, std::min(255, strengths[i])) : 40;
        if (a == 0) continue;
        uint32_t* row = dst + (size_t)y * stride;
        for (int x = 0; x < width; ++x) {
            const uint32_t c = row[x];
            const int r0 = (int)((c >> 16) & 0xFF);
            const int g0 = (int)((c >> 8) & 0xFF);
            const int b0 = (int)(c & 0xFF);
            int r = r0 + ((255 - 2 * r0) * a) / 255;
            int g = g0 + ((255 - 2 * g0) * a) / 255;
            int b = b0 + ((255 - 2 * b0) * a) / 255;
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            row[x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}

// Traced-peak ("harmonics") renderer: spectral peaks refined to sub-bin accuracy,
// splatted onto a pitch axis over a dimmed mel image. Constants mirror the
// reference tool in _pitch-curve-review/curve_core.py.
static void compositeReference(uint32_t* flat, int width, int height,
                               const unsigned char* coverA, const MelBank& bank,
                               const ViewParams& vp);

static void renderTraces(uint32_t* dst, int width, int height, size_t stride, const FrameStore& frames,
                         const MelBank& bank, const ViewParams& vp)
{
    uint32_t lut[1024];
    buildColorLut(vp, lut);
    const uint32_t bg = lut[0];
    for (int y = 0; y < height; ++y) {
        uint32_t* row = dst + (size_t)y * stride;
        for (int x = 0; x < width; ++x) row[x] = bg;
    }
    if (width < 2 || height < 2 || frames.count <= 0 || bank.bands <= 0) return;

    // The wash is context, not data: it falls back to the canvas colour quickly so
// silence really is paper, and it never reaches the dark end of the ramp (the
// peak lines own that). kBgFalloff > 1 pushes the low end toward the canvas,
// kBgCeil caps how far into the gradient the wash can go at all.
const float kBgFalloff = 2.0f;
const float kBgCeil = 0.42f;
    const float kCore = 0.75f;    // next row out from the traced peak
    const float kSkirt = 0.28f;   // one more row of skirt
    // Ink weight follows the peak's own level rather than saturating: with a plain gain
    // of 1.35 every partial above ~70 % of the level range clipped to full coverage, so
    // harmonics that get quieter with frequency all drew identically - the traces read
    // as on/off instead of as a falling series. A gamma keeps the top end solid and
    // opens the middle, so a partial 30 dB down is visibly lighter than the fundamental.
const float kGain = 1.3f;      // ceiling gain: only the loudest peaks reach full ink
const float kInkGamma = 1.35f;   // <1 flattens, >1 separates the quiet partials

    const int map = std::max(0, std::min(kNumMaps - 1, vp.cmap));
    // The peak line is drawn in this map's complementary ink, never in the hot
    // end of the gradient, so it reads as an overlay on the wash.
    const unsigned char* lineInk = kMapLine[map];

    const int B = bank.bands;
    const float range = std::max(1.0f, vp.ceilDb - vp.floorDb);
    const float invRange = 1.0f / range;
    const float bandsF = (float)B;

    static thread_local std::vector<uint16_t> bgIdx;
    static thread_local std::vector<unsigned char> cover;
    static thread_local std::vector<uint32_t> flat, blurTmp;
    static thread_local std::vector<int> yB0, yB1, xI0, xI1, yStep;
    static thread_local std::vector<float> yBfrac, yTilt, xFrac;
    static thread_local std::vector<char> xValid;

    static thread_local std::vector<float> washCurve;
    if (washCurve.empty()) {
        washCurve.resize(1024);
        for (int i = 0; i < 1024; ++i) {
            const float t = (float)i / 1023.0f;
            washCurve[(size_t)i] = std::pow(t, kBgFalloff) * kBgCeil;
        }
    }

    const size_t npix = (size_t)width * (size_t)height;
    bgIdx.assign(npix, 0);
    cover.assign(npix, 0);

    // ---- per-row mapping from pixel row to mel band
    yB0.resize((size_t)height);
    yB1.resize((size_t)height);
    yBfrac.resize((size_t)height);
    yTilt.resize((size_t)height);
    yStep.resize((size_t)height);
    for (int y = 0; y < height; ++y) {
        const float p = (float)(height - 1 - y) / (float)(height - 1);
        float pos = bandPosAtRow(clampf(p, 0.0f, 1.0f), vp.axis, vp.fmin, vp.fmax, bank);
        pos = clampf(pos, 0.0f, bandsF - 1.0f);
        int b0 = (int)pos;
        if (b0 > B - 1) b0 = B - 1;
        const int b1 = std::min(b0 + 1, B - 1);
        const float f = pos - (float)b0;
        yB0[(size_t)y] = b0;
        yB1[(size_t)y] = b1;
        yBfrac[(size_t)y] = f;
        const float oct0 = std::log2(std::max(1.0f, bank.centerHz[(size_t)b0]) / 1000.0f);
        const float oct1 = std::log2(std::max(1.0f, bank.centerHz[(size_t)b1]) / 1000.0f);
        yTilt[(size_t)y] = vp.tilt * (oct0 * (1.0f - f) + oct1 * f);

        const float hz = frequencyAtY((float)y, (float)height, bank, vp.axis, vp.fmin, vp.fmax);
        const float yUp = yForFrequency(hz * 1.05946309f, (float)height, bank, vp.axis, vp.fmin, vp.fmax);
        const float px = std::fabs((float)y - yUp);
        yStep[(size_t)y] = std::max(1, (int)std::lround(px / 12.0f));
    }

    // ---- per-column mapping from pixel column to analysis frame
    xI0.resize((size_t)width);
    xI1.resize((size_t)width);
    xFrac.resize((size_t)width);
    xValid.resize((size_t)width);
    const double t0 = frames.timeAt(0);
    for (int x = 0; x < width; ++x) {
        const double tq = vp.now - (double)vp.span * (double)(width - 1 - x) / (double)(width - 1);
        if (tq < t0) { // before the oldest frame: no data at all
            xValid[(size_t)x] = 0;
            xI0[(size_t)x] = xI1[(size_t)x] = 0;
            xFrac[(size_t)x] = 0.0f;
            continue;
        }
        int i0 = frames.lowerBound(tq);
        int i1 = std::min(i0 + 1, frames.count - 1);
        const double dt = frames.timeAt(i1) - frames.timeAt(i0);
        const float frac = dt > 1e-12 ? (float)((tq - frames.timeAt(i0)) / dt) : 0.0f;
        xValid[(size_t)x] = 1;
        xI0[(size_t)x] = i0;
        xI1[(size_t)x] = i1;
        xFrac[(size_t)x] = clampf(frac, 0.0f, 1.0f);
    }

    // ---- the dimmed mel spectrum: this is the only layer the glow touches

    for (int y = 0; y < height; ++y) {
        uint16_t* row = &bgIdx[(size_t)y * (size_t)width];
        const int b0 = yB0[(size_t)y], b1 = yB1[(size_t)y];
        const float bf = yBfrac[(size_t)y];
        const float base = yTilt[(size_t)y] - vp.floorDb;
        for (int x = 0; x < width; ++x) {
            if (!xValid[(size_t)x]) {
                row[x] = 0;
                continue;
            }
            const float* f0 = frames.frameAt(xI0[(size_t)x]);
            const float* f1 = frames.frameAt(xI1[(size_t)x]);
            const float tf = xFrac[(size_t)x];
            const float v0 = f0[b0] * (1.0f - bf) + f0[b1] * bf;
            const float v1 = f1[b0] * (1.0f - bf) + f1[b1] * bf;
            const float n = clampf((v0 * (1.0f - tf) + v1 * tf + base) * invRange, 0.0f, 1.0f);
            row[x] = (uint16_t)(washCurve[(size_t)(n * 1023.0f)] * 1023.0f);
        }
    }

    // ---- traced peaks, kept as coverage so they can be composited last
    // A trace that ends used to stop dead at full ink, which reads as a cut - most
    // visibly in the high range, where the peak tracer loses a partial quickly. Each
    // end now gets a short decaying tail, so the line dies away instead of clipping.
    constexpr int kTailPx = 11;
    constexpr float kTailTau = 3.0f;
    constexpr float kContinuityLog2 = 0.06f; // ~4 % in frequency counts as the same trace
    auto frameAtColumn = [&](int cx) {
        if (cx < 0 || cx >= width || !xValid[(size_t)cx]) return -1;
        return (xFrac[(size_t)cx] < 0.5f) ? xI0[(size_t)cx] : xI1[(size_t)cx];
    };

    for (int x = 0; x < width; ++x) {
        if (!xValid[(size_t)x]) continue;
        const int fi = (xFrac[(size_t)x] < 0.5f) ? xI0[(size_t)x] : xI1[(size_t)x];
        const int np = frames.peakCountAt(fi);
        if (np <= 0) continue;
        const Peak* pk = frames.peaksAt(fi);
        for (int i = 0; i < np; ++i) {
            if (pk[i].hz <= 0.0f) continue;
            const float ny = yForFrequency(pk[i].hz, (float)height, bank, vp.axis, vp.fmin, vp.fmax);
            const int yc = (int)std::lround(ny);
            if (yc < -8 || yc > height + 8) continue;
            const int ycClamped = std::max(0, std::min(height - 1, yc));
            const int step = yStep[(size_t)ycClamped];
            const float tilt = vp.tilt * std::log2(std::max(1.0f, pk[i].hz) / 1000.0f);
            // ink weight from the peak's own level, over the line range anchored at the
            // ceiling - independent of how wide the spectrum's Range is
            const float lineRange = std::max(1.0f, vp.lineRange);
            float n = (pk[i].db + tilt - (vp.ceilDb - lineRange)) / lineRange;
            if (!(n > 0.0f)) continue;
            n = std::min(1.0f, std::pow(n, kInkGamma) * kGain);
            const int span = 2 * step;
            for (int d = -span; d <= span; ++d) {
                const int y = yc + d;
                if (y < 0 || y >= height) continue;
                const int ad = std::abs(d);
                const float w = ad == 0 ? 1.0f : (ad <= step ? kCore : kSkirt);
                // a quiet partial is also thinner, not just lighter: on a 1-2 px line the
                // width carries more of the impression than the alpha alone
                float v = n * w * (0.72f + 0.28f * n);
                if (v > 1.0f) v = 1.0f;
                unsigned char& cell = cover[(size_t)y * (size_t)width + (size_t)x];
                const unsigned char iv = (unsigned char)(v * 255.0f);
                if (iv > cell) cell = iv;
            }

            // does this partial survive into the neighbouring column?
            const int fn = frameAtColumn(x + 1);
            bool continues = true;
            if (fn >= 0 && fn != fi) {
                continues = false;
                const int npn = frames.peakCountAt(fn);
                const Peak* pn = frames.peaksAt(fn);
                for (int j = 0; j < npn; ++j) {
                    if (pn[j].hz <= 0.0f) continue;
                    if (std::fabs(std::log2(pn[j].hz / pk[i].hz)) < kContinuityLog2) {
                        continues = true;
                        break;
                    }
                }
            }
            if (!continues) {
                for (int dx = 1; dx <= kTailPx; ++dx) {
                    const int xx = x + dx;
                    if (xx >= width) break;
                    const float f = std::exp(-(float)dx / kTailTau);
                    for (int d = -span; d <= span; ++d) {
                        const int y = yc + d;
                        if (y < 0 || y >= height) continue;
                        const int ad = std::abs(d);
                        const float w = ad == 0 ? 1.0f : (ad <= step ? kCore : kSkirt);
                        // a quiet partial is also thinner, not just lighter: on a 1-2 px line the
                        // width carries more of the impression than the alpha alone
                        float v = n * w * (0.62f + 0.38f * n) * f;
                        if (v > 1.0f) v = 1.0f;
                        unsigned char& cell = cover[(size_t)y * (size_t)width + (size_t)xx];
                        const unsigned char iv = (unsigned char)(v * 255.0f);
                        if (iv > cell) cell = iv;
                    }
                }
            }
        }
    }

    // ---- wash colour
    flat.assign(npix, bg);
    for (size_t i = 0; i < npix; ++i) flat[i] = lut[bgIdx[i]];

    // ---- glow: background only, so the peak lines stay perfectly crisp
    if (vp.glow > 0.001f) {
        const int radius = (int)std::lround(std::min(1.0f, vp.glow) * 10.0f);
        if (radius >= 1) {
            blurTmp.assign(npix, 0);
            blurPass(flat.data(), blurTmp.data(), width, height, radius);
        }
    }

    // ---- played notes, under the traces
    blendMidiNotes(flat.data(), width, height, (size_t)width, bg, bank, vp);

    // ---- composite the peaks in the complementary ink
    const uint32_t inkR = lineInk[0], inkG = lineInk[1], inkB = lineInk[2];
    for (size_t i = 0; i < npix; ++i) {
        const unsigned a = cover[i];
        if (!a) continue;
        uint32_t c = flat[i];
        const int r0 = (int)((c >> 16) & 0xFF), g0 = (int)((c >> 8) & 0xFF), b0 = (int)(c & 0xFF);
        const int r = r0 + ((int)inkR - r0) * (int)a / 255;
        const int g = g0 + ((int)inkG - g0) * (int)a / 255;
        const int b = b0 + ((int)inkB - b0) * (int)a / 255;
        flat[i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }

    for (int y = 0; y < height; ++y)
        std::memcpy(dst + (size_t)y * stride, &flat[(size_t)y * (size_t)width],
                    sizeof(uint32_t) * (size_t)width);
    // the reference input, over the primary one
    compositeReference(flat.data(), width, height, cover.data(), bank, vp);
}

void renderSpectrogram(uint32_t* dst, int width, int height, size_t stride, const FrameStore& frames,
                       const MelBank& bank, const ViewParams& vp)
{
    if (vp.mode == 1) renderTraces(dst, width, height, stride, frames, bank, vp);
    else renderSpectrum(dst, width, height, stride, frames, bank, vp);
}

FrequencyRange zoomFrequencyRange(float fmin, float fmax, int axis, float pointerPos01, float factor,
                                 float limitLo, float limitHi)
{
    FrequencyRange out{ fmin, fmax };
    if (!(fmax > fmin) || !(factor > 0.0f)) return out;
    const float p = clampf(pointerPos01, 0.0f, 1.0f);
    auto toAxis = [axis](float f) -> float {
        if (axis == 1) return std::log(std::max(1.0f, f));
        if (axis == 0) return (float)hzToMelSlaney(std::max(1.0f, f));
        return f;
    };
    auto fromAxis = [axis](float u) -> float {
        if (axis == 1) return (float)std::exp(u);
        if (axis == 0) return (float)melToHzSlaney(u);
        return u;
    };

    const float uMin = toAxis(fmin);
    const float uMax = toAxis(fmax);
    const float uAt = uMin + p * (uMax - uMin);
    float span = (uMax - uMin) * factor;

    const float uLo = toAxis(std::max(1.0f, limitLo));
    const float uHi = toAxis(std::max(1.0f, limitHi));
    const float spanMax = uHi - uLo;
    if (spanMax <= 0.0f) return out;
    // Zooming IN clamps the SPAN rather than the window, so the pointer frequency
    // stays pinned instead of sliding away. Zooming OUT must keep widening even when
    // the pointer would pin the window against a limit (that is what made "zoom out
    // sometimes does nothing"): there the window is allowed to slide.
    const bool zoomOut = factor > 1.0f;
    float allowed = spanMax;
    if (!zoomOut) {
        if (p > 1e-4f) allowed = std::min(allowed, (uAt - uLo) / p);
        if (p < 1.0f - 1e-4f) allowed = std::min(allowed, (uHi - uAt) / (1.0f - p));
    }
    const float spanMin = spanMax * 0.002f;
    span = clampf(span, spanMin, std::max(spanMin, allowed));

    float lo = uAt - p * span;
    float hi = lo + span;
    if (lo < uLo) lo = uLo;
    if (hi > uHi) hi = uHi;
    out.fmin = fromAxis(lo);
    out.fmax = fromAxis(hi);
    if (!(out.fmax > out.fmin)) return FrequencyRange{ fmin, fmax };
    return out;
}


// ---------------------------------------------------------------- reference input
// Input B gets its own pass: it has its own frames and timeline, so the column mapping
// is computed from B's own store. Traces only - a wash for both would turn the picture
// to mud - and where both inputs carry a partial the pixel takes the mix of the two
// inks, so overlap reads as a third colour instead of one source covering the other.
static void compositeReference(uint32_t* flat, int width, int height, const unsigned char* coverA,
                               const MelBank& bank, const ViewParams& vp)
{
    if (!vp.refFrames || vp.refFrames->count <= 1 || width < 4 || height < 4) return;
    if (vp.refStrength <= 0.01f) return;
    const FrameStore& rf = *vp.refFrames;

    unsigned char inkSec[3];
    secondaryTraceInk(vp.cmap, vp.light, vp.refInk, inkSec);
    const int cm = (vp.cmap < 0 || vp.cmap >= kNumMaps) ? 0 : vp.cmap;
    const unsigned char* inkPri = kMapLine[cm];

    const size_t npix = (size_t)width * (size_t)height;
    static thread_local std::vector<unsigned char> coverB;
    coverB.assign(npix, 0);

    static thread_local std::vector<int> stepOfRow;
    stepOfRow.resize((size_t)height);
    for (int y = 0; y < height; ++y) {
        const float hz = frequencyAtY((float)y, (float)height, bank, vp.axis, vp.fmin, vp.fmax);
        const float yUp = yForFrequency(hz * 1.05946309f, (float)height, bank, vp.axis, vp.fmin, vp.fmax);
        const float px = std::fabs((float)y - yUp);
        stepOfRow[(size_t)y] = std::max(1, (int)std::lround(px / 12.0f));
    }

    const float span = std::max(0.05f, vp.span);
    const float lineRange = std::max(1.0f, vp.lineRange);
    for (int x = 0; x < width; ++x) {
        const double tq = vp.now - (double)span * (double)(width - 1 - x) / (double)(width - 1);
        if (tq < rf.timeAt(0)) continue;
        const int fi = rf.lowerBound(tq);
        const int np = rf.peakCountAt(fi);
        if (np <= 0) continue;
        const Peak* pk = rf.peaksAt(fi);
        for (int i = 0; i < np; ++i) {
            if (pk[i].hz <= 0.0f) continue;
            const int yc = (int)std::lround(
                yForFrequency(pk[i].hz, (float)height, bank, vp.axis, vp.fmin, vp.fmax));
            if (yc < -8 || yc > height + 8) continue;
            const int step = stepOfRow[(size_t)std::max(0, std::min(height - 1, yc))];
            const float tilt = vp.tilt * std::log2(std::max(1.0f, pk[i].hz) / 1000.0f);
            float n = (pk[i].db + tilt - (vp.ceilDb - lineRange)) / lineRange;
            if (!(n > 0.0f)) continue;
            n = std::min(1.0f, std::pow(n, 1.35f) * 1.3f);
            for (int dd = -2 * step; dd <= 2 * step; ++dd) {
                const int y = yc + dd;
                if (y < 0 || y >= height) continue;
                const int ad = std::abs(dd);
                const float w = ad == 0 ? 1.0f : (ad <= step ? 0.75f : 0.28f);
                const float v = n * w * (0.72f + 0.28f * n);
                unsigned char& cell = coverB[(size_t)y * (size_t)width + (size_t)x];
                const unsigned char iv = (unsigned char)(std::min(1.0f, v) * 255.0f);
                if (iv > cell) cell = iv;
            }
        }
    }

    const float strength = clampf(vp.refStrength, 0.1f, 1.0f);
    for (size_t i = 0; i < npix; ++i) {
        const unsigned b = coverB[i];
        if (!b) continue;
        const unsigned a = coverA ? coverA[i] : 0u;
        const int r0 = (int)((flat[i] >> 16) & 0xFF);
        const int g0 = (int)((flat[i] >> 8) & 0xFF);
        const int b0 = (int)(flat[i] & 0xFF);
        int tr = inkSec[0], tg = inkSec[1], tb = inkSec[2];
        if (a) { // both inputs here: the average of the two inks
            tr = (inkPri[0] + inkSec[0]) / 2;
            tg = (inkPri[1] + inkSec[1]) / 2;
            tb = (inkPri[2] + inkSec[2]) / 2;
        }
        const unsigned alpha = (unsigned)((float)std::max(a, b) * strength);
        const int r = r0 + ((tr - r0) * (int)alpha) / 255;
        const int g = g0 + ((tg - g0) * (int)alpha) / 255;
        const int bb = b0 + ((tb - b0) * (int)alpha) / 255;
        flat[i] = 0xFF000000u | ((uint32_t)std::max(0, std::min(255, r)) << 16)
                  | ((uint32_t)std::max(0, std::min(255, g)) << 8)
                  | (uint32_t)std::max(0, std::min(255, bb));
    }
}

// ---------------------------------------------------------------- MIDI notes
void NoteTrace::noteOn(int pitch, double t)
{
    std::lock_guard<std::mutex> lk(mu_);
    // a repeated note-on for the same pitch ends the previous one (some hosts retrigger
    // without sending note-off first)
    for (auto& n : notes_)
        if (n.pitch == pitch && n.held) {
            n.held = false;
            n.end = t;
        }
    if ((int)notes_.size() >= kCapacity) notes_.erase(notes_.begin());
    MidiNote n;
    n.pitch = pitch;
    n.start = t;
    n.end = t;
    n.held = true;
    notes_.push_back(n);
}

void NoteTrace::noteOff(int pitch, double t)
{
    std::lock_guard<std::mutex> lk(mu_);
    for (auto it = notes_.rbegin(); it != notes_.rend(); ++it)
        if (it->pitch == pitch && it->held) {
            it->held = false;
            it->end = std::max(it->start, t);
            return;
        }
}

void NoteTrace::clear()
{
    std::lock_guard<std::mutex> lk(mu_);
    notes_.clear();
}

int NoteTrace::count() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return (int)notes_.size();
}

void NoteTrace::snapshot(std::vector<MidiNote>& out) const
{
    std::lock_guard<std::mutex> lk(mu_);
    out = notes_;
}

void NoteTrace::trim(double before)
{
    std::lock_guard<std::mutex> lk(mu_);
    notes_.erase(std::remove_if(notes_.begin(), notes_.end(),
                                [before](const MidiNote& n) { return !n.held && n.end < before; }),
                 notes_.end());
}

float midiNoteToHz(float note) { return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f); }

// Rotating the peak ink's hue gives a second ink that stays in the same "ink on paper"
// register as the first, while never colliding with it.
void secondaryTraceInk(int cmap, int light, int choice, unsigned char out[3])
{
    // fixed choices are mid-lightness so they read on a white and a black canvas alike
    static const unsigned char kFixed[4][3] = { { 0, 170, 190 }, { 232, 150, 30 },
                                                { 200, 60, 190 }, { 120, 200, 70 } };
    if (choice >= 1 && choice <= 4) {
        out[0] = kFixed[choice - 1][0];
        out[1] = kFixed[choice - 1][1];
        out[2] = kFixed[choice - 1][2];
        return;
    }
    if (cmap < 0 || cmap >= kNumMaps) cmap = 0;
    unsigned char r = kMapLine[cmap][0], g = kMapLine[cmap][1], b = kMapLine[cmap][2];
    const float rf = (float)r / 255.0f, gf = (float)g / 255.0f, bf = (float)b / 255.0f;
    const float mx = std::max(rf, std::max(gf, bf));
    const float mn = std::min(rf, std::min(gf, bf));
    float hue = 0.0f;
    float sat = mx > 0.0f ? (mx - mn) / mx : 0.0f;
    if (mx > mn) {
        if (mx == rf) hue = (gf - bf) / (mx - mn);
        else if (mx == gf) hue = 2.0f + (bf - rf) / (mx - mn);
        else hue = 4.0f + (rf - gf) / (mx - mn);
        hue /= 6.0f;
        if (hue < 0.0f) hue += 1.0f;
    }
    // a third of the wheel away, and a little lighter on a dark canvas so it reads
    hue = std::fmod(hue + 0.42f, 1.0f);
    if (sat < 0.12f) sat = 0.45f; // achromatic inks still need to be told apart
    const float val = light ? 0.46f : 0.92f;
    const float hh = hue * 6.0f;
    const int i = (int)hh;
    const float f = hh - (float)i;
    const float p = val * (1.0f - sat), q = val * (1.0f - sat * f), t = val * (1.0f - sat * (1.0f - f));
    float rr = 0, gg = 0, bb = 0;
    switch (i % 6) {
    case 0: rr = val; gg = t; bb = p; break;
    case 1: rr = q; gg = val; bb = p; break;
    case 2: rr = p; gg = val; bb = t; break;
    case 3: rr = p; gg = q; bb = val; break;
    case 4: rr = t; gg = p; bb = val; break;
    default: rr = val; gg = p; bb = q; break;
    }
    out[0] = (unsigned char)std::lround(clampf(rr, 0.0f, 1.0f) * 255.0f);
    out[1] = (unsigned char)std::lround(clampf(gg, 0.0f, 1.0f) * 255.0f);
    out[2] = (unsigned char)std::lround(clampf(bb, 0.0f, 1.0f) * 255.0f);
}


// Notes are drawn *behind* the image: a flat band per note at its pitch, mixed in after
// the wash (and after the glow, so it stays crisp) and before the peak traces. The mix
// is strongest where the spectrum is dark, so the notes read in the gaps but never
// compete with the material itself.
void blendMidiNotes(uint32_t* flat, int width, int height, size_t stride, uint32_t canvas,
                    const MelBank& bank, const ViewParams& vp)
{
    const int cr0 = (int)((canvas >> 16) & 0xFF), cg0 = (int)((canvas >> 8) & 0xFF),
              cb0 = (int)(canvas & 0xFF);
    if (!vp.notes || vp.noteCount <= 0 || vp.noteLevel <= 0.001f) return;
    if (width < 8 || height < 8) return;

    const float span = std::max(0.05f, vp.span);
    const double t0 = vp.now - (double)span;
    const float level = clampf(vp.noteLevel, 0.0f, 1.0f);
    const int noteR = vp.light ? 0x3c : 0xe4;
    const int noteG = vp.light ? 0x42 : 0xe2;
    const int noteB = vp.light ? 0x58 : 0xf0;

    for (int i = 0; i < vp.noteCount; ++i) {
        const MidiNote& n = vp.notes[i];
        const double end = n.held ? vp.now : n.end;
        if (end < t0 || n.start > vp.now) continue;

        const float xa = (float)((std::max(n.start, t0) - t0) / (double)span) * (float)(width - 1);
        const float xb = (float)((std::min(end, vp.now) - t0) / (double)span) * (float)(width - 1);
        int x0 = (int)std::floor(xa);
        int x1 = (int)std::ceil(xb);
        if (x1 < x0) x1 = x0;
        x0 = std::max(0, x0);
        x1 = std::min(width - 1, x1);
        if (x1 < x0) continue;

        // the note's own semitone band, through the same mapping as the image
        const float hzLo = midiNoteToHz((float)n.pitch - 0.5f);
        const float hzHi = midiNoteToHz((float)n.pitch + 0.5f);
        const float yA = yForFrequency(hzLo, (float)height, bank, vp.axis, vp.fmin, vp.fmax);
        const float yB = yForFrequency(hzHi, (float)height, bank, vp.axis, vp.fmin, vp.fmax);
        int yTop = (int)std::floor(std::min(yA, yB));
        int yBot = (int)std::ceil(std::max(yA, yB));
        yTop = std::max(0, yTop);
        yBot = std::min(height - 1, yBot);
        if (yBot < yTop) continue;

        // centre line, so the exact pitch is readable even when the band is several px
        const float yC = yForFrequency(midiNoteToHz((float)n.pitch), (float)height, bank, vp.axis,
                                       vp.fmin, vp.fmax);
        const int yLine = (int)std::lround(yC);

        for (int y = yTop; y <= yBot; ++y) {
            uint32_t* row = flat + (size_t)y * stride;
            const bool isLine = (y == yLine) && (yBot > yTop);
            for (int x = x0; x <= x1; ++x) {
                const uint32_t c = row[x];
                const int r0 = (int)((c >> 16) & 0xFF), g0 = (int)((c >> 8) & 0xFF), b0 = (int)(c & 0xFF);
                // how much room the image leaves here, measured as distance from the
                // canvas colour: quiet is "same as the canvas", on either theme
                const int dr = r0 - cr0, dg = g0 - cg0, db = b0 - cb0;
                const float dist = std::sqrt((float)(dr * dr + dg * dg + db * db)) / 441.0f;
                float a = level * (isLine ? 0.92f : 0.55f) * (1.0f - 0.92f * std::min(1.0f, dist));
                if (a <= 0.002f) continue;
                const int r = r0 + (int)(((float)noteR - (float)r0) * a);
                const int g = g0 + (int)(((float)noteG - (float)g0) * a);
                const int b = b0 + (int)(((float)noteB - (float)b0) * a);
                row[x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
    }
}

} // namespace mel


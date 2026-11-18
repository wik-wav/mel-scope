// selftest -- verifies the analyser is sample-rate independent and that the
// frequency mapping is correct, without any host or UI.
//
//   selftest
#include "../src/MelCore.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int g_fail = 0;

void report(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}

struct Result {
    float melHz = 0;     // centre frequency of the loudest mel band
    float peakHz = 0;    // strongest traced peak
    double lastTime = 0;
    double hopSeconds = 0;
    int frames = 0;
    int bands = 0;
    float fmaxUsed = 0;
    float bandSpacing = 1.0f;
};

Result run(double sr, float toneHz, int fftSize, int overlapIndex, int bands, float fmax)
{
    Result r;
    mel::AnalysisConfig cfg;
    cfg.sampleRate = sr;
    cfg.fftSize = fftSize;
    cfg.overlapIndex = overlapIndex;
    cfg.bands = bands;
    cfg.window = 0;
    cfg.fmin = 30.0f;
    cfg.fmax = fmax;
    cfg.smooth = 0.0f;

    mel::Analyzer an;
    an.configure(cfg);
    mel::FrameStore store;
    store.init(an.bank().bands, 4096);

    const int n = (int)(sr * 1.0);
    std::vector<float> buf((size_t)n);
    for (int i = 0; i < n; ++i)
        buf[(size_t)i] = 0.5f * (float)std::sin(2.0 * 3.14159265358979 * toneHz * i / sr);
    an.process(buf.data(), n, store);

    r.frames = store.count;
    r.bands = an.bank().bands;
    r.fmaxUsed = an.config().fmax;
    r.hopSeconds = (double)cfg.hopSize() / sr;
    if (store.count > 0) {
        r.lastTime = store.timeAt(store.count - 1);
        const float* f = store.frameAt(store.count - 1);
        int best = 0;
        for (int b = 1; b < store.bands; ++b)
            if (f[b] > f[best]) best = b;
        r.melHz = an.bank().centerHz[(size_t)best];
        r.bandSpacing = (best + 1 < store.bands)
                             ? an.bank().centerHz[(size_t)best + 1] - an.bank().centerHz[(size_t)best]
                             : (best > 0 ? an.bank().centerHz[(size_t)best] - an.bank().centerHz[(size_t)best - 1] : 1.0f);
        const mel::Peak* pk = store.peaksAt(store.count - 1);
        const int np = store.peakCountAt(store.count - 1);
        float bestAmp = -1e9f;
        for (int i = 0; i < np; ++i) {
            if (pk[i].db > bestAmp) {
                bestAmp = pk[i].db;
                r.peakHz = pk[i].hz;
            }
        }
    }
    return r;
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("== sample-rate / frequency mapping self-test ==\n");
    const double rates[] = { 22050.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    const float tones[] = { 110.0f, 440.0f, 1000.0f };

    for (double sr : rates) {
        for (float f : tones) {
            const Result r = run(sr, f, 8192, 2, 192, 1500.0f);
            const float melErr = std::fabs(r.melHz - f) / std::max(1.0f, r.bandSpacing); // in bands
            const float peakErr = std::fabs(r.peakHz - f) / f;
            char what[256];
            std::snprintf(what, sizeof what,
                          "sr %6.0f  %5.0f Hz -> mel band %7.1f Hz (%+.2f%%)  peak %7.2f Hz (%+.3f%%)  "
                          "frames %3d  t %.3f s  fmax %.0f",
                          sr, (double)f, (double)r.melHz, (double)(100.0f * (r.melHz - f) / f),
                          (double)r.peakHz, (double)(100.0f * (r.peakHz - f) / f), r.frames, r.lastTime,
                          (double)r.fmaxUsed);
            report(melErr <= 1.0f && peakErr < 0.005f, what);
        }
    }

    // high cut above Nyquist must be clamped, not crash or produce an empty image
    {
        const Result r = run(44100.0, 1000.0f, 8192, 2, 192, 40000.0f);
        char what[256];
        std::snprintf(what, sizeof what, "fmax above Nyquist clamps to %.0f Hz (<= 22050)", (double)r.fmaxUsed);
        report(r.fmaxUsed <= 22051.0f && r.melHz > 900.0f && r.melHz < 1100.0f, what);
    }

    // the time base must advance by hop/sr per frame
    {
        const Result r = run(48000.0, 440.0f, 2048, 1, 128, 8000.0f);
        const double expected = r.frames * r.hopSeconds;
        const double err = std::fabs(r.lastTime - expected);
        char what[256];
        std::snprintf(what, sizeof what, "frame clock: %d frames * %.5f s = %.3f s, got %.3f s",
                      r.frames, r.hopSeconds, expected, r.lastTime);
        report(err < 0.05, what);
    }

    // every FFT size and band count must build and analyse
    {
        int nFft = 0;
        const int* ffts = mel::fftSizeTable(nFft);
        int nBands = 0;
        const int* bands = mel::bandsTable(nBands);
        bool ok = true;
        for (int i = 0; i < nFft; ++i) {
            for (int j = 0; j < nBands; j += 4) {
                const Result r = run(48000.0, 500.0f, ffts[i], 1, bands[j], 4000.0f);
                if (r.frames <= 0 || !std::isfinite(r.melHz) || r.melHz < 300.0f || r.melHz > 800.0f) ok = false;
            }
        }
        report(ok, "all FFT sizes x band counts analyse a 500 Hz tone correctly");
    }

    // level mapping must not push digital silence into the middle of the ramp
    {
        const mel::LevelRange quiet = mel::computeRange(72.0f, -6.0f, true, -110.0f);
        char what[256];
        std::snprintf(what, sizeof what, "collapsed auto ceiling: ceil %.0f floor %.0f (silence -130 dB must be off-scale)",
                      (double)quiet.ceilDb, (double)quiet.floorDb);
        report(quiet.floorDb >= -108.5f && quiet.ceilDb >= -80.5f && quiet.floorDb > -130.0f, what);

        const mel::LevelRange loud = mel::computeRange(72.0f, -6.0f, true, -18.0f);
        std::snprintf(what, sizeof what, "normal auto ceiling unaffected: ceil %.0f floor %.0f",
                      (double)loud.ceilDb, (double)loud.floorDb);
        report(std::fabs(loud.ceilDb + 18.0f) < 0.5f && std::fabs(loud.floorDb + 90.0f) < 0.5f, what);
    }

    // vertical zoom must keep the frequency under the pointer fixed
    {
        bool ok = true;
        char detail[256] = "";
        for (int axis = 0; axis < 3; ++axis) {
            for (float pos : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
                for (float factor : { 0.5f }) {
                    const float fmin = 40.0f, fmax = 5000.0f;
                    const mel::FrequencyRange r =
                        mel::zoomFrequencyRange(fmin, fmax, axis, pos, factor, 10.0f, 24000.0f);
                    auto at = [axis](float a, float b, float p) {
                        auto u = [axis](float f) {
                            if (axis == 1) return (float)std::log(std::max(1.0f, f));
                            if (axis == 0) return (float)mel::hzToMelSlaney(std::max(1.0f, f));
                            return f;
                        };
                        return u(a) + p * (u(b) - u(a));
                    };
                    auto inv = [axis](float u) {
                        if (axis == 1) return (float)std::exp(u);
                        if (axis == 0) return (float)mel::melToHzSlaney(u);
                        return u;
                    };
                    const float before = inv(at(fmin, fmax, pos));
                    const float after = inv(at(r.fmin, r.fmax, pos));
                    const float rel = std::fabs(after - before) / std::max(1.0f, before);
                    if (!(rel < 1e-3f)) {
                        ok = false;
                        std::snprintf(detail, sizeof detail, "axis %d pos %.2f factor %.2f: %.1f -> %.1f Hz",
                                      axis, (double)pos, (double)factor, (double)before, (double)after);
                    }
                }
            }
        }
        report(ok, ok ? "zoom in keeps the frequency under the pointer" : detail);
    }

    // zooming out must always be able to widen, even when the pointer pins the
    // window against the frequency limits
    {
        bool grew = true;
        float fromSpan = std::log(800.0f / 30.0f), toSpan = 0.0f;
        mel::FrequencyRange r{ 30.0f, 800.0f };
        for (int step = 0; step < 8; ++step)
            r = mel::zoomFrequencyRange(r.fmin, r.fmax, 1, 0.72f, 1.3f, 10.0f, 24000.0f);
        toSpan = std::log(r.fmax / r.fmin);
        if (!(toSpan > fromSpan * 1.3f)) grew = false;
        char what[192];
        std::snprintf(what, sizeof what, "zoom out widens past a pinned edge: %.0f-%.0f Hz (span %.2f -> %.2f)",
                      (double)r.fmin, (double)r.fmax, (double)fromSpan, (double)toSpan);
        report(grew, what);
    }

    // ---- MIDI note trace: the layer drawn under the spectrum depends on this
    {
        mel::NoteTrace nt;
        std::vector<mel::MidiNote> snap;
        nt.noteOn(60, 1.0);
        nt.noteOff(60, 1.5);
        nt.noteOn(64, 1.2);            // still held
        nt.snapshot(snap);
        bool found60 = false, found64 = false, held64 = false, ended60 = false;
        for (const auto& n : snap) {
            if (n.pitch == 60) { found60 = true; ended60 = (!n.held && std::fabs(n.end - 1.5) < 1e-9); }
            if (n.pitch == 64) { found64 = true; held64 = n.held; }
        }
        char what[256];
        std::snprintf(what, sizeof what, "note trace keeps %d notes, held flag %d/%d",
                      (int)snap.size(), (int)held64, (int)ended60);
        report(snap.size() == 2 && found60 && found64, what);
        report(ended60 && held64, "note-off ends the note, unreleased note stays held");

        nt.noteOn(64, 2.0);           // retrigger without a note-off
        nt.snapshot(snap);
        int held64count = 0;
        for (const auto& n : snap)
            if (n.pitch == 64 && n.held) ++held64count;
        report(held64count == 1, "retrigger closes the previous note");

        nt.trim(1.6);
        nt.snapshot(snap);
        bool sawHeld = false, sawOld = false;
        for (const auto& n : snap) {
            if (n.held) sawHeld = true;
            if (n.pitch == 60) sawOld = true;
        }
        report(!sawOld, "trim drops ended notes");
        report(sawHeld, "trim keeps held notes");

        mel::NoteTrace big;
        for (int i = 0; i < mel::NoteTrace::kCapacity + 40; ++i) big.noteOn(30 + (i % 60), 1.0 + i);
        big.snapshot(snap);
        std::snprintf(what, sizeof what, "note trace holds %d of a %d cap", (int)snap.size(),
                      mel::NoteTrace::kCapacity);
        report((int)snap.size() <= mel::NoteTrace::kCapacity, what);
    }

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}

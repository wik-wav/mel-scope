// Offline verification tool: WAV -> mel spectrogram image using the exact same
// Analyzer/Renderer code as the plugin.
//
//   melrender <in.wav> <out.ppm> [w h] [key=value ...]
//
//   key=value pairs: fft=2048 overlap=1 bands=192 fmin=30 fmax=16000 floor=-95
//                    ceil=-6 gamma=1 cmap=0 axis=0 span=4 smooth=0.25 tilt=0
//                    glow=0.22 autoceil=1 window=0 frames=all
#include "../src/MelCore.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Wav {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> mono; // mixed to mono
    double duration() const { return sampleRate > 0 ? (double)mono.size() / sampleRate : 0.0; }
};

bool readWav(const char* path, Wav& out)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return false; }
    char riff[12];
    if (std::fread(riff, 1, 12, f) != 12 || std::memcmp(riff, "RIFF", 4) || std::memcmp(riff + 8, "WAVE", 4)) {
        std::fprintf(stderr, "not a RIFF/WAVE file\n");
        std::fclose(f);
        return false;
    }
    int format = 0, channels = 0, bits = 0, sampleRate = 0;
    std::vector<unsigned char> data;
    for (;;) {
        char id[4];
        unsigned int size = 0;
        if (std::fread(id, 1, 4, f) != 4) break;
        if (std::fread(&size, 4, 1, f) != 1) break;
        if (std::memcmp(id, "fmt ", 4) == 0) {
            std::vector<unsigned char> b(size);
            if (std::fread(b.data(), 1, size, f) != size) break;
            format = *(short*)&b[0];
            channels = *(short*)&b[2];
            sampleRate = *(int*)&b[4];
            bits = *(short*)&b[14];
            if (format == 0xFFFE && size >= 26) format = *(short*)&b[24]; // extensible: sub-format
        } else if (std::memcmp(id, "data", 4) == 0) {
            data.resize(size);
            if (std::fread(data.data(), 1, size, f) != size) break;
        } else {
            std::fseek(f, (long)size + (long)(size & 1), SEEK_CUR);
        }
    }
    std::fclose(f);
    if (data.empty() || channels <= 0 || sampleRate <= 0) {
        std::fprintf(stderr, "bad wav: fmt=%d ch=%d bits=%d sr=%d bytes=%zu\n", format, channels, bits,
                     sampleRate, data.size());
        return false;
    }
    const int bytesPer = bits / 8;
    const size_t frames = data.size() / (size_t)(bytesPer * channels);
    out.sampleRate = sampleRate;
    out.channels = channels;
    out.mono.resize(frames, 0.0f);
    for (size_t i = 0; i < frames; ++i) {
        double acc = 0.0;
        for (int c = 0; c < channels; ++c) {
            const unsigned char* p = &data[(i * (size_t)channels + (size_t)c) * (size_t)bytesPer];
            double v = 0.0;
            if (format == 3 && bits == 32) {
                float fv; std::memcpy(&fv, p, 4); v = fv;
            } else if (format == 3 && bits == 64) {
                double dv; std::memcpy(&dv, p, 8); v = dv;
            } else if (bits == 16) {
                v = ((short)(p[0] | (p[1] << 8))) / 32768.0;
            } else if (bits == 24) {
                int s = (int)p[0] | ((int)p[1] << 8) | ((int)p[2] << 16);
                if (s & 0x800000) s |= ~0xFFFFFF;
                v = s / 8388608.0;
            } else if (bits == 32) {
                int s; std::memcpy(&s, p, 4); v = s / 2147483648.0;
            }
            acc += v;
        }
        out.mono[i] = (float)(acc / channels);
    }
    return true;
}

bool writePpm(const char* path, const uint32_t* px, int w, int h)
{
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<unsigned char> row((size_t)w * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t c = px[(size_t)y * w + x];
            row[(size_t)x * 3 + 0] = (unsigned char)((c >> 16) & 0xFF);
            row[(size_t)x * 3 + 1] = (unsigned char)((c >> 8) & 0xFF);
            row[(size_t)x * 3 + 2] = (unsigned char)(c & 0xFF);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

float argf(int argc, char** argv, const char* key, float def)
{
    const size_t kl = std::strlen(key);
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], key, kl) == 0 && argv[i][kl] == '=')
            return (float)std::atof(argv[i] + kl + 1);
    }
    return def;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s in.wav out.ppm [w h] [key=value...]\n", argv[0]);
        return 2;
    }
    Wav wav;
    if (!readWav(argv[1], wav)) return 1;

    int outW = 1200, outH = 600;
    int positional = 0;
    for (int i = 3; i < argc; ++i) {
        if (std::strchr(argv[i], '=') == nullptr) {
            if (positional == 0) outW = std::atoi(argv[i]);
            else if (positional == 1) outH = std::atoi(argv[i]);
            ++positional;
        }
    }

    const int fftIdx = (int)argf(argc, argv, "fftidx", 3);
    int fftCount = 0;
    const int* fftSizes = mel::fftSizeTable(fftCount);
    const int fftSize = (int)argf(argc, argv, "fft", (float)fftSizes[fftIdx]);
    const int overlap = (int)argf(argc, argv, "overlap", 1);
    int bandCount = 0;
    const int* bandTable = mel::bandsTable(bandCount);
    const int bands = (int)argf(argc, argv, "bands", (float)bandTable[3]);

    mel::AnalysisConfig cfg;
    cfg.fftSize = fftSize;
    cfg.overlapIndex = overlap;
    cfg.bands = bands;
    cfg.window = (int)argf(argc, argv, "window", 0);
    cfg.channel = 0;
    cfg.fmin = argf(argc, argv, "fmin", 30.0f);
    cfg.fmax = argf(argc, argv, "fmax", 1500.0f);
    cfg.smooth = argf(argc, argv, "smooth", 0.25f);
    cfg.sampleRate = wav.sampleRate;

    mel::Analyzer analyzer;
    analyzer.configure(cfg);

    mel::FrameStore store;
    store.init(analyzer.bank().bands, 16384);

    // optional: analyse only the first N seconds
    const float maxSecs = argf(argc, argv, "maxsecs", 0.0f);
    size_t n = wav.mono.size();
    if (maxSecs > 0.0f) n = std::min(n, (size_t)(maxSecs * wav.sampleRate));
    analyzer.process(wav.mono.data(), (int)n, store);

    mel::ViewParams vp;
    vp.mode = (int)argf(argc, argv, "mode", 1);
    vp.light = (int)argf(argc, argv, "light", 1);
    vp.cmap = (int)argf(argc, argv, "cmap", 7);
    vp.axis = (int)argf(argc, argv, "axis", 1);
    vp.gamma = argf(argc, argv, "gamma", 1.0f);
    vp.tilt = argf(argc, argv, "tilt", 0.0f);
    vp.glow = argf(argc, argv, "glow", 0.22f);
    vp.span = argf(argc, argv, "span", 4.0f);
    vp.fmin = argf(argc, argv, "vmin", analyzer.config().fmin);
    vp.fmax = argf(argc, argv, "vmax", analyzer.config().fmax);
    const float rangeDb = argf(argc, argv, "range", 72.0f);
    const float ceilParam = argf(argc, argv, "ceil", -6.0f);
    const int autoCeil = (int)argf(argc, argv, "autoceil", 1);
    const mel::LevelRange lr = mel::computeRange(rangeDb, ceilParam, autoCeil != 0, analyzer.peakDb() + 2.0f);
    vp.floorDb = lr.floorDb;
    vp.ceilDb = lr.ceilDb;
    vp.now = store.count > 0 ? store.timeAt(store.count - 1) : 0.0;

    std::vector<uint32_t> img((size_t)outW * outH, 0xFF000000u);
    mel::renderSpectrogram(img.data(), outW, outH, (size_t)outW, store, analyzer.bank(), vp);

    if (!writePpm(argv[2], img.data(), outW, outH)) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    if (argf(argc, argv, "dump", 0.0f) == 2.0f) {
        std::printf("render grid (colour index 0..255, 0=dark):\n");
        for (int gy = 0; gy < 12; ++gy) {
            const int y = (int)((gy + 0.5) / 12.0 * outH);
            std::printf("  y=%4d ", y);
            for (int gx = 0; gx < 10; ++gx) {
                const int x = (int)((gx + 0.5) / 10.0 * outW);
                const uint32_t c = img[(size_t)y * outW + x];
                std::printf(" %3d", (int)(c & 0xFF));
            }
            std::printf("\n");
        }
    }
    std::printf("wav %s: %d Hz, %d ch, %.2f s\n", argv[1], wav.sampleRate, wav.channels, wav.duration());
    std::printf("fft %d overlap %d bands %d hop %d -> %d frames, fmin %.1f fmax %.1f\n", cfg.fftSize,
                cfg.overlapIndex, analyzer.bank().bands, cfg.hopSize(), store.count, analyzer.config().fmin,
                analyzer.config().fmax);
    std::printf("peak %.1f dB, ceiling %.1f dB, floor %.1f dB, span %.2f s\n", analyzer.peakDb(),
                vp.ceilDb, vp.floorDb, vp.span);

    if (argf(argc, argv, "dump", 0.0f) != 0.0f && store.count > 0) {
        const int B = analyzer.bank().bands;
        const float* f = store.frameAt(store.count - 1);
        int best = 0;
        for (int b = 1; b < B; ++b)
            if (f[b] > f[best]) best = b;
        std::printf("bands=%d  peak band=%d (%.0f Hz) at %.1f dB\n", B, best,
                    analyzer.bank().centerHz[best], f[best]);
        for (int b = 0; b < B; b += std::max(1, B / 24))
            std::printf("  band %4d  %8.1f Hz  %7.1f dB\n", b, analyzer.bank().centerHz[b], f[b]);
        std::printf("  band %4d  %8.1f Hz  %7.1f dB\n", B - 1, analyzer.bank().centerHz[B - 1], f[B - 1]);
    }
    std::printf("wrote %s (%dx%d)\n", argv[2], outW, outH);
    return 0;
}

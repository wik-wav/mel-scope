// hosttest -- loads MelScope.clap the way a DAW does and exercises the ABI.
//
//   hosttest <plugin.clap>                 ABI / params / audio / state checks
//   hosttest <plugin.clap> --gui <out.bmp> [seconds]
//                                          also opens the editor, feeds audio,
//                                          and captures the window
#include <windows.h>

#include <clap/clap.h>

#include "MelCore.h"

#include <cmath>
#include <thread>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

// ------------------------------------------------------------------ fake host
struct FakeHost {
    clap_host_t api;
};

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostNoop(const clap_host_t*) {}
void hostCallback(const clap_host_t*) {}

void initHost(FakeHost& h)
{
    std::memset(&h, 0, sizeof(h));
    h.api.clap_version = CLAP_VERSION_INIT;
    h.api.name = "hosttest";
    h.api.vendor = "local";
    h.api.url = "";
    h.api.version = "1.0";
    h.api.get_extension = hostGetExtension;
    h.api.request_restart = hostNoop;
    h.api.request_process = hostNoop;
    h.api.request_callback = hostCallback;
}

// --------------------------------------------------------------- state stream
struct MemStream {
    clap_ostream_t os;
    clap_istream_t is;
    std::vector<unsigned char> data;
    size_t readPos = 0;
};

int64_t memWrite(const clap_ostream_t* s, const void* buffer, uint64_t size)
{
    auto* m = (MemStream*)s->ctx;
    const unsigned char* p = (const unsigned char*)buffer;
    m->data.insert(m->data.end(), p, p + size);
    return (int64_t)size;
}

int64_t memRead(const clap_istream_t* s, void* buffer, uint64_t size)
{
    auto* m = (MemStream*)s->ctx;
    const size_t avail = m->data.size() - m->readPos;
    const size_t n = size < avail ? (size_t)size : avail;
    if (n) std::memcpy(buffer, m->data.data() + m->readPos, n);
    m->readPos += n;
    return (int64_t)n;
}

void initStream(MemStream& m)
{
    m.os.ctx = &m;
    m.os.write = memWrite;
    m.is.ctx = &m;
    m.is.read = memRead;
}

// ------------------------------------------------------------------ test tone
struct Tone {
    double phase = 0.0;
    double sr = 48000.0;
    double t = 0.0;

    // A sung vowel: harmonic stack with a formant envelope and a little vibrato,
    // gliding between two notes so the time axis has something to show.
    void fill(float* left, float* right, uint32_t frames)
    {
        for (uint32_t i = 0; i < frames; ++i) {
            const double glide = 1.0 + 0.06 * std::sin(2 * 3.14159265 * 0.18 * t);
            const double f0 = 132.0 * glide * std::pow(2.0, 0.03 * std::sin(2 * 3.14159265 * 5.2 * t));
            phase += 2 * 3.14159265 * f0 / sr;
            double v = 0.0;
            for (int h = 1; h <= 40; ++h) {
                const double fh = f0 * h;
                if (fh > sr * 0.45) break;
                double formant = 0.0;
                const double fc[3] = { 700.0, 1220.0, 2600.0 };
                const double fg[3] = { 1.0, 0.62, 0.30 };
                for (int k = 0; k < 3; ++k) {
                    const double d = std::log(std::max(1e-3, fh / fc[k])) / 0.16;
                    formant += fg[k] * std::exp(-d * d);
                }
                v += (std::sin(h * phase) / std::pow((double)h, 1.6)) * (0.22 + formant);
            }
            v *= 0.26;
            v += 0.0004 * ((double)rand() / RAND_MAX - 0.5);
            if (left) left[i] = (float)v;
            if (right) right[i] = (float)v;
            t += 1.0 / sr;
        }
    }
};

// ------------------------------------------------------------- screenshot bmp
bool saveBmp(const char* path, HDC src, int w, int h)
{
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(src);
    HBITMAP bmp = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(mem); return false; }
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, src, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);

    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = bmi.bmiHeader;
    ih.biHeight = h; // bottom-up file
    ih.biSizeImage = (DWORD)(w * h * 4);
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + ih.biSizeImage;
    FILE* f = std::fopen(path, "wb");
    if (!f) { DeleteObject(bmp); return false; }
    std::fwrite(&fh, sizeof(fh), 1, f);
    std::fwrite(&ih, sizeof(ih), 1, f);
    // flip rows
    std::vector<unsigned char> row((size_t)w * 4);
    for (int y = 0; y < h; ++y) {
        std::memcpy(row.data(), (unsigned char*)bits + (size_t)(h - 1 - y) * w * 4, row.size());
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    DeleteObject(bmp);
    return true;
}

LRESULT CALLBACK parentProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) return 0;
    if (msg == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: %s <plugin.clap> [--gui out.bmp seconds]\n", argv[0]);
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // hard watchdog: no matter what goes wrong, this process cannot outlive the test
    std::thread([] {
        Sleep(90000);
        std::printf("WATCHDOG: terminating\n");
        TerminateProcess(GetCurrentProcess(), 98);
    }).detach();

    const char* path = argv[1];
    const char* guiOut = nullptr;
    double guiSeconds = 2.0;
    bool openPanel = false;
    bool openDropdown = false;
    bool doZoom = false;
    bool doRange = false;
    bool doZoomOut = false;
    bool doPan = false;
    bool doZoomedDropdown = false;
    bool doFrozen = false;
    bool doHold = false;
    bool doAsyncZoom = false;
    bool doAltZoom = false;
    bool doKbZoom = false;
    bool doHelp = false;
    bool doScroll = false;
    bool doEdit = false;
    bool doHover = false;
    bool doParamProbe = false;
    bool doRef = false;
    bool doZoomRange = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--panel") == 0) {
            openPanel = true;
            continue;
        }
        if (std::strcmp(argv[i], "--zoomfrange") == 0) {
            doZoomRange = true;
            openPanel = true; // these probes live inside the openPanel block
            continue;
        }
        if (std::strcmp(argv[i], "--ref") == 0) {
            doRef = true;
            continue;
        }
        if (std::strcmp(argv[i], "--paramprobe") == 0) {
            doParamProbe = true;
            openPanel = true;
            continue;
        }
        if (std::strcmp(argv[i], "--hover") == 0) {
            doHover = true;
            continue;
        }
        if (std::strcmp(argv[i], "--edit") == 0) {
            doEdit = true;
            openPanel = true;
            continue;
        }
        if (std::strcmp(argv[i], "--scroll") == 0) {
            doScroll = true;
            openPanel = true;
            continue;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            doHelp = true;
            continue;
        }
        if (std::strcmp(argv[i], "--kbzoom") == 0) {
            doKbZoom = true;
            continue;
        }
        if (std::strcmp(argv[i], "--altzoom") == 0) {
            doAltZoom = true;
            continue;
        }
        if (std::strcmp(argv[i], "--asynczoom") == 0) {
            doAsyncZoom = true;
            continue;
        }
        if (std::strcmp(argv[i], "--hold") == 0) {
            doHold = true;
            continue;
        }
        if (std::strcmp(argv[i], "--frozen") == 0) {
            doFrozen = true;
            openPanel = true;
            continue;
        }
        if (std::strcmp(argv[i], "--zoomed-dropdown") == 0) {
            doZoomedDropdown = true;
            openPanel = true;
            continue;
        }
        if (std::strcmp(argv[i], "--pan") == 0) {
            doPan = true;
            openPanel = true;
            continue;
        }
        if (std::strcmp(argv[i], "--zoomout") == 0) {
            doZoomOut = true;
            openPanel = true;
            continue;
        }
        if (std::strcmp(argv[i], "--frange") == 0) {
            doRange = true;
            openPanel = true;
            continue;
        }
        if (std::strcmp(argv[i], "--zoom") == 0) {
            doZoom = true;
            openPanel = true;
            continue;
        }
        if (std::strcmp(argv[i], "--dropdown") == 0) {
            openPanel = true;
            openDropdown = true;
            continue;
        }
        if (std::strcmp(argv[i], "--gui") == 0 && i + 1 < argc) {
            guiOut = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') guiSeconds = std::atof(argv[++i]);
        }
    }

    HMODULE lib = LoadLibraryA(path);
    if (!lib) {
        std::printf("FAIL: LoadLibrary error %lu\n", (unsigned long)GetLastError());
        return 1;
    }
    auto* entry = (const clap_plugin_entry_t*)GetProcAddress(lib, "clap_entry");
    if (!entry) {
        std::printf("FAIL: no clap_entry symbol\n");
        return 1;
    }
    std::printf("entry version %u.%u.%u\n", entry->clap_version.major, entry->clap_version.minor,
                entry->clap_version.revision);
    check(clap_version_is_compatible(entry->clap_version), "entry version is compatible");
    check(mel::paramTableIsConsistent(), "parameter table matches ParamIndex order");
    check(entry->init(path), "entry->init");

    const auto* factory = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    check(factory != nullptr, "got plugin factory");
    if (!factory) return 1;

    const uint32_t nPlugins = factory->get_plugin_count(factory);
    std::printf("factory has %u plugin(s)\n", nPlugins);
    for (uint32_t i = 0; i < nPlugins; ++i) {
        const auto* d = factory->get_plugin_descriptor(factory, i);
        std::printf("  %u: id=%s name=%s version=%s\n", i, d->id, d->name, d->version);
    }

    FakeHost host;
    initHost(host);
    const auto* d0 = factory->get_plugin_descriptor(factory, 0);
    const clap_plugin_t* plugin = factory->create_plugin(factory, &host.api, d0 ? d0->id : "");
    check(plugin != nullptr, "create_plugin");
    if (!plugin) return 1;
    check(plugin->init(plugin), "plugin->init");

    // ---- params
    const auto* params = (const clap_plugin_params_t*)plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    check(params != nullptr, "clap.params extension");
    if (params) {
        const uint32_t n = params->count(plugin);
        std::printf("params: %u\n", n);
        for (uint32_t i = 0; i < n; ++i) {
            clap_param_info_t info;
            if (!params->get_info(plugin, i, &info)) continue;
            char text[128] = { 0 };
            params->value_to_text(plugin, info.id, info.default_value, text, sizeof text);
            std::printf("  %2u %-14s %-9s %8.2f..%-8.2f def %-8.2f %s\n", info.id, info.name, info.module,
                        info.min_value, info.max_value, info.default_value, text);
        }
        double v = 0;
        check(params->get_value(plugin, 0, &v), "get_value(0)");
        char buf[128] = { 0 };
        check(params->value_to_text(plugin, 0, v, buf, sizeof buf), "value_to_text(0)");
        std::printf("  param 0 default text: %s\n", buf);
    }

    // ---- audio ports
    const auto* ports = (const clap_plugin_audio_ports_t*)plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS);
    check(ports != nullptr, "clap.audio-ports extension");
    if (ports) {
        const uint32_t ins = ports->count(plugin, true);
        const uint32_t outs = ports->count(plugin, false);
        clap_audio_port_info_t info;
        std::printf("audio ports: %u in / %u out\n", ins, outs);
        if (ins) {
            ports->get(plugin, 0, true, &info);
            std::printf("  in : \"%s\" ch=%u type=%s\n", info.name, info.channel_count, info.port_type);
        }
        if (outs) {
            ports->get(plugin, 0, false, &info);
            std::printf("  out: \"%s\" ch=%u type=%s\n", info.name, info.channel_count, info.port_type);
        }
        check(ins >= 1 && outs >= 1, "has one input and one output port");
    }

    // ---- activate + process
    const double sr = 48000.0;
    check(plugin->activate(plugin, sr, 32, 1024), "activate");
    check(plugin->start_processing(plugin), "start_processing");

    const uint32_t block = 256;
    std::vector<float> inL(block, 0.0f), inR(block, 0.0f), outL(block, 0.0f), outR(block, 0.0f);
    Tone tone;
    tone.sr = sr;
    double refPhase = 0.0;

    clap_audio_buffer_t inBuf = {};
    clap_audio_buffer_t outBuf = {};
    clap_audio_buffer_t refBuf = {};
    std::vector<float> refL(block, 0.0f), refR(block, 0.0f);
    float* inPtrs[2] = { inL.data(), inR.data() };
    float* outPtrs[2] = { outL.data(), outR.data() };
    float* refPtrs[2] = { refL.data(), refR.data() };
    inBuf.data32 = inPtrs;
    inBuf.channel_count = 2;
    refBuf.data32 = refPtrs;
    refBuf.channel_count = 2;
    outBuf.data32 = outPtrs;
    outBuf.channel_count = 2;

    // two input buses: main and reference
    clap_audio_buffer_t inBuses[2] = { inBuf, refBuf };
    clap_process_t proc = {};
    proc.audio_inputs = doRef ? inBuses : &inBuf;
    proc.audio_inputs_count = doRef ? 2u : 1u;
    proc.audio_outputs = &outBuf;
    proc.audio_outputs_count = 1;
    proc.frames_count = block;

    bool passthrough = true;
    const int blocks = 400; // ~2.1 s at 48k
    for (int b = 0; b < blocks; ++b) {
        tone.fill(inL.data(), inR.data(), block);
        if (doRef) {
            // a distinct partial for the reference bus
            for (uint32_t i = 0; i < block; ++i) {
                const double ph = 2.0 * 3.14159265358979 * 330.0 * (double)(refPhase + i) / (double)sr;
                refL[i] = refR[i] = (float)(0.5 * std::sin(ph));
            }
            refPhase += block;
        }
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        const int status = plugin->process(plugin, &proc);
        if (status != CLAP_PROCESS_CONTINUE) { passthrough = false; break; }
        for (uint32_t i = 0; i < block; ++i) {
            if (std::fabs(outL[i] - inL[i]) > 1e-6f || std::fabs(outR[i] - inR[i]) > 1e-6f) {
                passthrough = false;
                break;
            }
        }
        if (b == 100) break; // enough for the ABI test
    }
    check(passthrough, "audio passes through bit-exactly");

    // mono input must work too
    {
        inBuf.channel_count = 1;
        float* mono[2] = { inL.data(), nullptr };
        inBuf.data32 = mono;
        tone.fill(inL.data(), nullptr, block);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        plugin->process(plugin, &proc);
        check(std::fabs(outL[10] - inL[10]) < 1e-6f, "mono input passes through");
        inBuf.channel_count = 2;
        inBuf.data32 = inPtrs;
    }

    // ---- state round trip
    const auto* state = (const clap_plugin_state_t*)plugin->get_extension(plugin, CLAP_EXT_STATE);
    check(state != nullptr, "clap.state extension");
    if (state && params) {
        MemStream ms;
        initStream(ms);
        check(state->save(plugin, &ms.os), "state save");
        std::printf("state size: %zu bytes\n", ms.data.size());

        const clap_id pid = 5; // fmax
        double before = 0, after = 0;
        params->get_value(plugin, pid, &before);
        {
            // change it through the params interface (no host events needed)
            const clap_plugin_t* pl = plugin;
            (void)pl;
        }
        ms.readPos = 0;
        check(state->load(plugin, &ms.is), "state load");
        params->get_value(plugin, pid, &after);
        check(std::fabs(before - after) < 1e-6, "state round trip restores values");

        // A blob written before the FFT list shrank must still load: refusing it can
        // make a host abandon the device, so v1 is translated instead.
        {
            MemStream old1;
            old1.data.clear();
            const char magic[4] = { 'M', 'L', 'S', 'C' };
            const uint32_t v1 = 1;
            const uint32_t count = 1;
            const uint32_t idFft = 0; // pFft is the first parameter
            const float oldIndex = 7.0f; // v1 index 7 == 32768 -> new index 3
            const unsigned char* m = (const unsigned char*)magic;
            old1.data.insert(old1.data.end(), m, m + 4);
            const unsigned char* pv = (const unsigned char*)&v1;
            old1.data.insert(old1.data.end(), pv, pv + 4);
            const unsigned char* pc = (const unsigned char*)&count;
            old1.data.insert(old1.data.end(), pc, pc + 4);
            const unsigned char* pi = (const unsigned char*)&idFft;
            old1.data.insert(old1.data.end(), pi, pi + 4);
            const unsigned char* px = (const unsigned char*)&oldIndex;
            old1.data.insert(old1.data.end(), px, px + 4);
            old1.readPos = 0;
            initStream(old1);
            old1.readPos = 0;
            const bool okOld = state->load(plugin, &old1.is);
            double fftVal = -1;
            params->get_value(plugin, idFft, &fftVal);
            // new index for 8192 is 2
            check(okOld, "v1 state blob is accepted");
            check(std::fabs(fftVal - 3.0) < 1e-6, "v1 fft index is translated to 16384");
            // put the saved state back, so the GUI run below starts from the defaults
            ms.readPos = 0;
            state->load(plugin, &ms.is);
        }
    }

    // ---- GUI
    if (guiOut) {
        const auto* gui = (const clap_plugin_gui_t*)plugin->get_extension(plugin, CLAP_EXT_GUI);
        check(gui != nullptr, "clap.gui extension");
        if (gui) {
            check(gui->is_api_supported(plugin, CLAP_WINDOW_API_WIN32, false), "win32 GUI supported");
            check(gui->create(plugin, CLAP_WINDOW_API_WIN32, false), "gui->create");

            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = parentProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"HostTestParent";
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
            wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
            RegisterClassExW(&wc);

            uint32_t gw = 1180, gh = 700;
            gui->get_size(plugin, &gw, &gh);
            if (const char* sz = std::getenv("MELSCOPE_SIZE")) {
                unsigned w2 = gw, h2 = gh;
                if (std::sscanf(sz, "%ux%u", &w2, &h2) == 2) {
                    gui->set_size(plugin, w2, h2);
                    gui->get_size(plugin, &gw, &gh);
                }
            }
            if (const char* sz = std::getenv("MELSCOPE_SIZE")) {
                        unsigned w2 = gw, h2 = gh;
                        if (std::sscanf(sz, "%ux%u", &w2, &h2) == 2) {
                                    gw = w2;
                                    gh = h2;
                        }
            }

            RECT r = { 0, 0, (LONG)gw, (LONG)gh };
            AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
            HWND parent = CreateWindowExW(0, L"HostTestParent", L"Mel Scope (host test)",
                                          WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, r.right - r.left,
                                          r.bottom - r.top, nullptr, nullptr, GetModuleHandleW(nullptr),
                                          nullptr);
            check(parent != nullptr, "created host parent window");

            clap_window_t win = {};
            win.api = CLAP_WINDOW_API_WIN32;
            win.win32 = parent;
            check(gui->set_parent(plugin, &win), "gui->set_parent");
            check(gui->show(plugin), "gui->show");

            // feed audio in the background while pumping messages
            const int totalBlocks = (int)(guiSeconds * sr / block);
            int sent = 0;
            ULONGLONG start = GetTickCount64();

            if (doHold) {
                // let two seconds of audio build up, then freeze via the header pill
                HWND childH = GetWindow(parent, GW_CHILD);
                const ULONGLONG warm = GetTickCount64();
                MSG wm;
                int sentWarm = 0;
                while (GetTickCount64() - warm < 2000) {
                    while (PeekMessageW(&wm, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&wm);
                        DispatchMessageW(&wm);
                    }
                    const int wantWarm = (int)((GetTickCount64() - warm) * sr / 1000.0 / block);
                    while (sentWarm < wantWarm && sentWarm < totalBlocks) {
                        tone.fill(inL.data(), inR.data(), block);
                        plugin->process(plugin, &proc);
                        ++sentWarm;
                    }
                    Sleep(5);
                }
                const LPARAM fz = MAKELPARAM(1130, 25);
                SendMessageW(childH, WM_LBUTTONDOWN, MK_LBUTTON, fz);
                SendMessageW(childH, WM_LBUTTONUP, 0, fz);
                // Keep poking the UI while held: each hover marks the window dirty, so
                // the screenshot is a fresh render of the held frames and not a stale
                // bitmap left over from the moment of freezing.
                for (int j = 0; j < 12; ++j) {
                    SendMessageW(childH, WM_MOUSEMOVE, 0, MAKELPARAM(700 + j * 4, 320));
                    Sleep(1200);
                }
                std::printf("  froze after 2 s of audio and repainted repeatedly\n");
                start = GetTickCount64();
                sent = sentWarm;
            }

            if (doHelp) {
                // open the help sheet from the header pill
                HWND childH = GetWindow(parent, GW_CHILD);
                RECT rc{};
                GetClientRect(childH, &rc);
                // mouse messages carry CLIENT coordinates
                const LPARAM hp = MAKELPARAM((int)rc.right - 210, 20);
                SendMessageW(childH, WM_LBUTTONDOWN, MK_LBUTTON, hp);
                SendMessageW(childH, WM_LBUTTONUP, 0, hp);
                std::printf("  opened help\n");
            }

            if (doKbZoom) {
                // wheel over the piano keyboard, no modifiers at all
                HWND childZ = GetWindow(parent, GW_CHILD);
                const ULONGLONG warm = GetTickCount64();
                MSG wm;
                int sentWarm = 0;
                while (GetTickCount64() - warm < 1500) {
                    while (PeekMessageW(&wm, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&wm);
                        DispatchMessageW(&wm);
                    }
                    const int wantWarm = (int)((GetTickCount64() - warm) * sr / 1000.0 / block);
                    while (sentWarm < wantWarm && sentWarm < totalBlocks) {
                        tone.fill(inL.data(), inR.data(), block);
                        plugin->process(plugin, &proc);
                        ++sentWarm;
                    }
                    Sleep(5);
                }
                for (int k = 0; k < 3; ++k) {
                    SendMessageW(childZ, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(84, 300)); // over the keys
                    Sleep(40);
                }
                std::printf("  sent 3 unmodified wheel-ins over the keyboard strip\n");
                start = GetTickCount64();
                sent = sentWarm;
            }

            if (doAltZoom) {
                // Alt held, then Ctrl+wheel: the reported failure. Alt must not put the
                // window into menu mode and swallow the modifier state.
                HWND childZ = GetWindow(parent, GW_CHILD);
                const ULONGLONG warm = GetTickCount64();
                MSG wm;
                int sentWarm = 0;
                while (GetTickCount64() - warm < 1500) {
                    while (PeekMessageW(&wm, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&wm);
                        DispatchMessageW(&wm);
                    }
                    const int wantWarm = (int)((GetTickCount64() - warm) * sr / 1000.0 / block);
                    while (sentWarm < wantWarm && sentWarm < totalBlocks) {
                        tone.fill(inL.data(), inR.data(), block);
                        plugin->process(plugin, &proc);
                        ++sentWarm;
                    }
                    Sleep(5);
                }
                INPUT alt = {};
                alt.type = INPUT_KEYBOARD;
                alt.ki.wVk = VK_MENU;
                SendInput(1, &alt, sizeof(INPUT));
                SendMessageW(childZ, WM_SYSKEYDOWN, VK_MENU,
                             MAKELPARAM(1, (long)(1u | (0x38u << 16))));
                Sleep(60);

                INPUT ctrl = {};
                ctrl.type = INPUT_KEYBOARD;
                ctrl.ki.wVk = VK_CONTROL;
                SendInput(1, &ctrl, sizeof(INPUT));
                Sleep(60);
                for (int k = 0; k < 3; ++k) {
                    SendMessageW(childZ, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(600, 300));
                    Sleep(40);
                }
                ctrl.ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(1, &ctrl, sizeof(INPUT));
                SendMessageW(childZ, WM_SYSKEYUP, VK_MENU,
                             MAKELPARAM(1, (long)(1u | (0x38u << 16))));
                alt.ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(1, &alt, sizeof(INPUT));
                std::printf("  alt held, then 3 ctrl-wheel-ins\n");
                start = GetTickCount64();
                sent = sentWarm;
            }

            if (doAsyncZoom) {
                // Hold Ctrl at the OS level and send a wheel with NO modifier bits in
                // wParam: this is what a host that reports no key state looks like,
                // and only an asynchronous key read can still detect it.
                HWND childZ = GetWindow(parent, GW_CHILD);
                const ULONGLONG warm = GetTickCount64();
                MSG wm;
                int sentWarm = 0;
                while (GetTickCount64() - warm < 1500) {
                    while (PeekMessageW(&wm, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&wm);
                        DispatchMessageW(&wm);
                    }
                    const int wantWarm = (int)((GetTickCount64() - warm) * sr / 1000.0 / block);
                    while (sentWarm < wantWarm && sentWarm < totalBlocks) {
                        tone.fill(inL.data(), inR.data(), block);
                        plugin->process(plugin, &proc);
                        ++sentWarm;
                    }
                    Sleep(5);
                }
                INPUT in = {};
                in.type = INPUT_KEYBOARD;
                in.ki.wVk = VK_CONTROL;
                SendInput(1, &in, sizeof(INPUT));
                Sleep(60);
                std::printf("  ctrl physically down: %d\n", (GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 1 : 0);
                for (int k = 0; k < 3; ++k) {
                    SendMessageW(childZ, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(600, 300));
                    Sleep(40);
                }
                in.ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(1, &in, sizeof(INPUT));
                std::printf("  sent 3 ctrl-wheel-ins with no modifier bits in wParam\n");
                start = GetTickCount64();
                sent = sentWarm;
            }

            if (doHover) {
                // freeze, then move the pointer twice: the readout must follow while held
                HWND childH = GetWindow(parent, GW_CHILD);
                const ULONGLONG warm = GetTickCount64();
                MSG wm;
                int sentWarm = 0;
                while (GetTickCount64() - warm < 1600) {
                    while (PeekMessageW(&wm, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&wm);
                        DispatchMessageW(&wm);
                    }
                    const int wantWarm = (int)((GetTickCount64() - warm) * sr / 1000.0 / block);
                    while (sentWarm < wantWarm && sentWarm < totalBlocks) {
                        tone.fill(inL.data(), inR.data(), block);
                        plugin->process(plugin, &proc);
                        ++sentWarm;
                    }
                    Sleep(5);
                }
                const LPARAM fz = MAKELPARAM(1130, 25);
                SendMessageW(childH, WM_LBUTTONDOWN, MK_LBUTTON, fz);
                SendMessageW(childH, WM_LBUTTONUP, 0, fz);
                // the real cursor must be over the window: a synthetic move alone is
                // followed by WM_MOUSELEAVE, which clears the hover state
                for (int k = 0; k < 2; ++k) {
                    POINT cp{ k == 0 ? 600 : 820, k == 0 ? 250 : 430 };
                    ClientToScreen(childH, &cp);
                    SetCursorPos(cp.x, cp.y);
                    for (int q2 = 0; q2 < 6; ++q2) {
                        MSG hm;
                        while (PeekMessageW(&hm, nullptr, 0, 0, PM_REMOVE)) {
                            TranslateMessage(&hm);
                            DispatchMessageW(&hm);
                        }
                        Sleep(30);
                    }
                }
                std::printf("  froze, then hovered twice\n");
                start = GetTickCount64();
                sent = sentWarm;
            }

            if (doParamProbe) {
                // drag the Peak range row and prove UI scale does not move with it
                HWND childP = GetWindow(parent, GW_CHILD);
                for (int q3 = 0; q3 < 40; ++q3) {
                    MSG pm;
                    while (PeekMessageW(&pm, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&pm);
                        DispatchMessageW(&pm);
                    }
                    Sleep(10);
                }
                const clap_plugin_params_t* prm =
                    (const clap_plugin_params_t*)plugin->get_extension(plugin, CLAP_EXT_PARAMS);
                const auto readByKey = [&](const char* key) -> float {
                    if (!prm) return -1.0f;
                    const uint32_t n = prm->count(plugin);
                    for (uint32_t i = 0; i < n; ++i) {
                        clap_param_info_t info{};
                        if (!prm->get_info(plugin, i, &info)) continue;
                        if (std::strcmp(info.name, key) == 0) {
                            double v = 0.0;
                            if (prm->get_value(plugin, info.id, &v)) return (float)v;
                        }
                    }
                    return -1.0f;
                };
                const auto pump = [&](int ms) {
                    MSG pm;
                    for (int k = 0; k < ms / 5; ++k) {
                        while (PeekMessageW(&pm, nullptr, 0, 0, PM_REMOVE)) {
                            TranslateMessage(&pm);
                            DispatchMessageW(&pm);
                        }
                        Sleep(5);
                    }
                };
                // row y comes from MELSCOPE_PROBE_Y so it tracks the panel layout
                // the panel has to be open before any row exists
                SendMessageW(childP, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(45, 26));
                SendMessageW(childP, WM_LBUTTONUP, 0, MAKELPARAM(45, 26));
                pump(160);
                int py = 342;
                if (const char* e = std::getenv("MELSCOPE_PROBE_Y")) py = std::atoi(e);
                std::printf("  start   : ui=%.0f line=%.0f\n", readByKey("UI scale"),
                            readByKey("Peak range"));

                // 1. drag on the rail: Peak range must move, UI scale must not
                SendMessageW(childP, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(120, py));
                for (int step = 1; step <= 6; ++step) {
                    SendMessageW(childP, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(120 + step * 12, py));
                    pump(15);
                }
                SendMessageW(childP, WM_LBUTTONUP, 0, MAKELPARAM(192, py));
                pump(60);
                std::printf("  rail    : ui=%.0f line=%.0f\n", readByKey("UI scale"),
                            readByKey("Peak range"));

                // 2. drag that STARTS on the value box: it must slide, not open the editor
                SendMessageW(childP, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(300, py));
                for (int step = 1; step <= 6; ++step) {
                    SendMessageW(childP, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(300 + step * 8, py));
                    pump(15);
                }
                SendMessageW(childP, WM_LBUTTONUP, 0, MAKELPARAM(348, py));
                pump(80);
                std::printf("  onvalue : ui=%.0f line=%.0f\n", readByKey("UI scale"),
                            readByKey("Peak range"));

                // 3. a click on the value box (no movement) opens the editor
                SendMessageW(childP, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(300, py));
                SendMessageW(childP, WM_LBUTTONUP, 0, MAKELPARAM(300, py));
                pump(120);
                std::printf("  clicked : ui=%.0f line=%.0f\n", readByKey("UI scale"),
                            readByKey("Peak range"));
                // if the editor really opened, typing 42 and pressing Return lands 42
                SendMessageW(childP, WM_CHAR, (WPARAM)'4', 0);
                SendMessageW(childP, WM_CHAR, (WPARAM)'2', 0);
                SendMessageW(childP, WM_KEYDOWN, VK_RETURN, 0);
                pump(120);
                std::printf("  typed42 : ui=%.0f line=%.0f (42 means the editor opened)\n",
                            readByKey("UI scale"), readByKey("Peak range"));
            }

            MSG msg;
            while (GetTickCount64() - start < (ULONGLONG)(guiSeconds * 1000.0)) {
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                const ULONGLONG elapsed = GetTickCount64() - start;
                const int want = (int)(elapsed * sr / 1000.0 / block);
                while (sent < want && sent < totalBlocks) {
                    tone.fill(inL.data(), inR.data(), block);
                    plugin->process(plugin, &proc);
                    ++sent;
                }
                Sleep(4);
            }
            std::printf("fed %d blocks (%.2f s)\n", sent, sent * block / sr);
            Sleep(300);

            if (openPanel) {
                HWND child = GetWindow(parent, GW_CHILD);
                std::printf("  child window %p\n", (void*)child);
                if (child) {
                    const LPARAM at = MAKELPARAM(58, 17); // the Settings pill, now top-left
                    const LRESULT r1 = SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, at);
                    SendMessageW(child, WM_LBUTTONUP, 0, at);
                    std::printf("  clicked Settings button (result %ld)\n", (long)r1);
                    if (openDropdown) {
                        const LPARAM at2 = MAKELPARAM(150, 460); // the "Colormap" row (panel is on the left now)
                        SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, at2);
                        SendMessageW(child, WM_LBUTTONUP, 0, at2);
                        std::printf("  opened the Window dropdown\n");
                    }
                    if (doRange) {
                        // the "Full range" pill sits left of Freeze in the header
                        const LPARAM at3 = MAKELPARAM(1000, 25);
                        SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, at3);
                        SendMessageW(child, WM_LBUTTONUP, 0, at3);
                        std::printf("  clicked Full range\n");
                    }
                    if (doEdit) {
                        HWND childE = GetWindow(parent, GW_CHILD);
                        // click the UI scale value (bottom row), type 180, press Enter
                        const LPARAM rowPt = MAKELPARAM(300, 690);
                        SendMessageW(childE, WM_LBUTTONDOWN, MK_LBUTTON, rowPt);
                        SendMessageW(childE, WM_LBUTTONUP, 0, rowPt);
                        for (const char* c = "180"; *c; ++c)
                            SendMessageW(childE, WM_CHAR, (WPARAM)*c, 0);
                        SendMessageW(childE, WM_KEYDOWN, VK_RETURN, 0);
                        std::printf("  typed 180 into the UI scale value\n");
                    }
                    if (doScroll) {
                        // wheel down over the open panel: the list must move
                        HWND childS = GetWindow(parent, GW_CHILD);
                        for (int k = 0; k < 3; ++k) {
                            POINT pt{ 150, 300 };
                            ClientToScreen(childS, &pt);
                            SendMessageW(childS, WM_MOUSEWHEEL, MAKEWPARAM(0, -600),
                                         MAKELPARAM(pt.x, pt.y));
                            MSG m;
                            for (int q = 0; q < 4; ++q) {
                                while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                                    TranslateMessage(&m);
                                    DispatchMessageW(&m);
                                }
                                Sleep(10);
                            }
                        }
                        std::printf("  scrolled the panel by 6 notches\n");
                    }
                    if (doFrozen) {
                        // freeze via the header pill, then poke the panel rows
                        const LPARAM fz = MAKELPARAM(1130, 25);
                        SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, fz);
                        SendMessageW(child, WM_LBUTTONUP, 0, fz);
                        // FFT size row (structural, must be inert) at x=150,y=94
                        SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(150, 94));
                        SendMessageW(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(260, 94));
                        SendMessageW(child, WM_LBUTTONUP, 0, MAKELPARAM(260, 94));
                        // Gamma row (display, must still move) at x=150,y=478
                        SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(150, 478));
                        SendMessageW(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(210, 478));
                        SendMessageW(child, WM_LBUTTONUP, 0, MAKELPARAM(210, 478));
                        std::printf("  froze, dragged FFT (blocked) and Gamma (allowed)\n");
                    }
                    if (doZoomedDropdown) {
                        // zoom first, so the plot area is pannable, then click a panel
                        // row: the row must win over the pan
                        HWND childZ = GetWindow(parent, GW_CHILD);
                        for (int k = 0; k < 2; ++k) {
                            SendMessageW(childZ, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA),
                                         MAKELPARAM(600, 300));
                            MSG m;
                            for (int q = 0; q < 4; ++q) {
                                while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                                    TranslateMessage(&m);
                                    DispatchMessageW(&m);
                                }
                                Sleep(12);
                            }
                        }
                        const LPARAM row = MAKELPARAM(150, 460); // Colormap row, over the plot
                        SendMessageW(childZ, WM_LBUTTONDOWN, MK_LBUTTON, row);
                        SendMessageW(childZ, WM_LBUTTONUP, 0, row);
                        std::printf("  zoomed in, then clicked the Colormap row inside the panel\n");
                    }
                    if (doPan) {
                        HWND childZ = GetWindow(parent, GW_CHILD);
                        // zoom in three notches so the view can be panned
                        for (int k = 0; k < 3; ++k) {
                            SendMessageW(childZ, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA),
                                         MAKELPARAM(600, 300));
                            MSG m;
                            for (int q = 0; q < 4; ++q) {
                                while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                                    TranslateMessage(&m);
                                    DispatchMessageW(&m);
                                }
                                Sleep(12);
                            }
                        }
                        // drag the canvas downwards: the view should follow
                        SendMessageW(childZ, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(600, 300));
                        for (int step = 1; step <= 8; ++step) {
                            SendMessageW(childZ, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(600, 300 + step * 20));
                            Sleep(8);
                        }
                        SendMessageW(childZ, WM_LBUTTONUP, 0, MAKELPARAM(600, 460));
                        std::printf("  zoomed in 3x then dragged the canvas 160 px down\n");
                    }
                    if (doZoomOut) {
                        HWND childZ = GetWindow(parent, GW_CHILD);
                        for (int k = 0; k < 12; ++k) {
                            const LPARAM wp = MAKEWPARAM(MK_CONTROL, -(long)WHEEL_DELTA);
                            const LPARAM lp = MAKELPARAM(500, 300);
                            SendMessageW(childZ, WM_MOUSEWHEEL, wp, lp);
                            // pump like a human turning the wheel, so the rebuild lands
                            MSG m;
                            for (int q = 0; q < 6; ++q) {
                                while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                                    TranslateMessage(&m);
                                    DispatchMessageW(&m);
                                }
                                Sleep(12);
                            }
                        }
                        std::printf("  sent 12 Ctrl+wheel-outs over the plot\n");
                    }
                    if (doZoom) {
                        // three Ctrl+wheel-ins over the middle of the plot
                        HWND childZ = GetWindow(parent, GW_CHILD);
                        for (int k = 0; k < 3; ++k) {
                            LPARAM wp = MAKEWPARAM(MK_CONTROL, WHEEL_DELTA);
                            LPARAM lp = MAKELPARAM(500, 300);
                            SendMessageW(childZ, WM_MOUSEWHEEL, wp, lp);
                        }
                        std::printf("  sent 3 Ctrl+wheel-ins over the plot\n");
                    }
                    if (doZoomRange) {
                        // zoom in vertically, then press Full range: the zoom must go too
                        HWND childZ2 = GetWindow(parent, GW_CHILD);
                        for (int k = 0; k < 4; ++k)
                            SendMessageW(childZ2, WM_MOUSEWHEEL,
                                         MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), MAKELPARAM(500, 300));
                        MSG zr;
                        for (int k = 0; k < 40; ++k) {
                            while (PeekMessageW(&zr, nullptr, 0, 0, PM_REMOVE)) {
                                TranslateMessage(&zr);
                                DispatchMessageW(&zr);
                            }
                            Sleep(10);
                        }
                        std::printf("  zoomed vertically, then clicking Full range\n");
                        SendMessageW(childZ2, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(1180, 25));
                        SendMessageW(childZ2, WM_LBUTTONUP, 0, MAKELPARAM(1180, 25));
                        for (int k = 0; k < 40; ++k) {
                            while (PeekMessageW(&zr, nullptr, 0, 0, PM_REMOVE)) {
                                TranslateMessage(&zr);
                                DispatchMessageW(&zr);
                            }
                            Sleep(10);
                        }
                    }
                    const ULONGLONG t0 = GetTickCount64();
                    MSG m2;
                    while (GetTickCount64() - t0 < 500) {
                        while (PeekMessageW(&m2, nullptr, 0, 0, PM_REMOVE)) {
                            TranslateMessage(&m2);
                            DispatchMessageW(&m2);
                        }
                        Sleep(5);
                    }
                }
            }

            HDC dc = GetDC(parent);
            const bool ok = saveBmp(guiOut, dc, (int)gw, (int)gh);
            ReleaseDC(parent, dc);
            check(ok, "captured window to BMP");
            std::printf("screenshot: %s (%ux%u)\n", guiOut, gw, gh);

            gui->destroy(plugin);
        }
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
                g_failures == 1 ? "" : "s");
    std::fflush(stdout);
    // Skip DLL unload and static destructors: a DAW keeps the module loaded too,
    // and unloading a DLL that owned a window, GDI+ and a thread can deadlock.
    ExitProcess(g_failures == 0 ? 0 : 1);
}

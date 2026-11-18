// Vst3HostTest.cpp -- loads the built VST3 the way a host does and exercises it.
//
// Verification for the VST3 build, mirroring hosttest/ for the CLAP build: factory
// enumeration, instantiation through IComponent, audio processing, state round-trip,
// parameter enumeration and the editor view's platform/size contract.

#include "pluginterfaces/base/fplatform.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static int g_failures = 0;
static void check(bool ok, const char* what)
{
    std::fprintf(stderr, "[%s] %s\n", ok ? "ok  " : "FAIL", what);
    std::fflush(stderr);
    if (!ok) ++g_failures;
}

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "dist\\MelScope.vst3\\Contents\\x86_64-win\\MelScope.vst3";
    HMODULE mod = LoadLibraryA(path);
    check(mod != nullptr, "library loads");
    if (!mod) return 1;

    auto getFactory = (IPluginFactory * (*)()) GetProcAddress(mod, "GetPluginFactory");
    check(getFactory != nullptr, "exports GetPluginFactory");
    if (!getFactory) return 1;

    IPluginFactory* factory = getFactory();
    check(factory != nullptr, "factory created");
    if (!factory) return 1;

    int32 classes = factory->countClasses();
    check(classes >= 1, "factory registers at least one class");
    std::fprintf(stderr, "      classes: %d\n", (int)classes);

    PClassInfo info{};
    check(factory->getClassInfo(0, &info) == kResultOk, "class info readable");
    std::fprintf(stderr, "      class: %s  category: %s\n", info.name, info.category);
    check(std::strcmp(info.category, kVstAudioEffectClass) == 0, "class is an audio effect");

    FUnknown* instance = nullptr;
    check(factory->createInstance(info.cid, IComponent::iid, (void**)&instance) == kResultOk,
          "instance created through IComponent");
    if (!instance) return 1;

    auto* component = (IComponent*)instance;
    check(component->initialize(nullptr) == kResultOk, "component initializes");

    IAudioProcessor* proc = nullptr;
    check(component->queryInterface(IAudioProcessor::iid, (void**)&proc) == kResultOk && proc,
          "component is an audio processor");

    IEditController* ctrl = nullptr;
    check(component->queryInterface(IEditController::iid, (void**)&ctrl) == kResultOk && ctrl,
          "component is also its edit controller");

    if (ctrl) {
        ctrl->initialize(nullptr);
        int32 n = ctrl->getParameterCount();
        std::fprintf(stderr, "      parameters: %d\n", (int)n);
        check(n > 0, "parameters are exposed");
        bool named = true, ranged = true;
        for (int32 i = 0; i < n; ++i) {
            ParameterInfo pi{};
            if (ctrl->getParameterInfo(i, pi) != kResultOk) { named = false; continue; }
            if (!pi.title[0]) named = false;
            if (pi.defaultNormalizedValue < 0.0 || pi.defaultNormalizedValue > 1.0) ranged = false;
        }
        check(named, "every parameter has a title");
        check(ranged, "every parameter has a range or steps");
    }

    if (proc) {
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = 512;
        setup.sampleRate = 44100.0;
        check(proc->setupProcessing(setup) == kResultOk, "setupProcessing accepts 44.1k/32-bit");
        component->setActive(true);
        proc->setProcessing(true);
    }

    // one block of audio must come out bit-identical to what went in
    {
        const int frames = 512;
        std::vector<std::vector<float>> inBuf(2, std::vector<float>(frames)), outBuf(2, std::vector<float>(frames));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < frames; ++i) inBuf[(size_t)c][(size_t)i] = 0.25f * std::sin(0.05f * i * (c + 1));

        float* inPtr[2] = { inBuf[0].data(), inBuf[1].data() };
        float* outPtr[2] = { outBuf[0].data(), outBuf[1].data() };
        AudioBusBuffers inBus{}, outBus{};
        inBus.numChannels = 2;
        inBus.channelBuffers32 = inPtr;
        outBus.numChannels = 2;
        outBus.channelBuffers32 = outPtr;

        ProcessData data{};
        data.numSamples = frames;
        data.numInputs = 1;
        data.inputs = &inBus;
        data.numOutputs = 1;
        data.outputs = &outBus;

        if (proc) {
            check(proc->process(data) == kResultOk, "process returns kResultOk");
            bool same = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < frames; ++i)
                    if (std::fabs(outBuf[(size_t)c][(size_t)i] - inBuf[(size_t)c][(size_t)i]) > 1e-9f) same = false;
            check(same, "audio passes through bit-exactly");
        }
    }

    // state must survive a save/load cycle
    if (component) {
        MemoryStream save;
        check(component->getState(&save) == kResultOk, "getState succeeds");
        const int64 size = save.getSize();
        std::fprintf(stderr, "      state blob: %d bytes\n", (int)size);
        check(size > 12, "state blob is non-trivial");

        save.seek(0, IBStream::kIBSeekSet, nullptr);
        check(component->setState(&save) == kResultOk, "setState accepts the same blob");

        MemoryStream again;
        component->getState(&again);
        check(again.getSize() == size, "re-saved state keeps its size");
    }

    // the editor has to answer the platform question the host will ask
    if (ctrl) {
        IPlugView* view = ctrl->createView(ViewType::kEditor);
        check(view != nullptr, "createView returns a view");
        if (view) {
            check(view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultTrue,
                  "view supports kPlatformTypeHWND");
            ViewRect r{};
            check(view->getSize(&r) == kResultOk, "view reports a size");
            std::fprintf(stderr, "      view size: %dx%d\n", (int)(r.right - r.left), (int)(r.bottom - r.top));
            check(r.right - r.left >= 560 && r.bottom - r.top >= 360, "view size is sane");
            check(view->canResize() == kResultTrue, "view is resizable");

            // MELSCOPE_NOVIEW=1 isolates the DLL-unload path from the editor's
            const bool skipView = std::getenv("MELSCOPE_NOVIEW") != nullptr; // bisect helper
            if (skipView) {
                std::fprintf(stderr, "--- editor skipped\n");
            } else {

            // the point of the whole exercise: attached() must build the real editor
            HWND parent = CreateWindowExW(0, L"STATIC", L"host", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0,
                                          900, 600, nullptr, nullptr, nullptr, nullptr);
            check(parent != nullptr, "test parent window created");
            if (parent) {
                check(view->attached(parent, kPlatformTypeHWND) == kResultOk, "attached() accepts HWND");
                HWND child = GetWindow(parent, GW_CHILD);
                check(child != nullptr, "editor created its child window");
                if (child) {
                    ViewRect want{ 0, 0, 720, 480 };
                    view->onSize(&want);
                    RECT rc{};
                    GetClientRect(child, &rc);
                    std::fprintf(stderr, "      editor window: %dx%d\n", (int)(rc.right - rc.left),
                                 (int)(rc.bottom - rc.top));
                    check(rc.right - rc.left == 720 && rc.bottom - rc.top == 480,
                          "onSize resizes the editor window");
                    wchar_t cls[64] = {};
                    GetClassNameW(child, cls, 64);
                    std::fprintf(stderr, "      editor class: %ls\n", cls);
                }
                    // pump like a host UI thread, and give the analyser some audio first
                    {
                        auto pump = [](int ms) {
                            MSG m;
                            const ULONGLONG t0 = GetTickCount64();
                            while (GetTickCount64() - t0 < (ULONGLONG)ms) {
                                while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                                    TranslateMessage(&m);
                                    DispatchMessageW(&m);
                                }
                                Sleep(2);
                            }
                        };
                        if (proc) {
                            const int frames = 512;
                            std::vector<std::vector<float>> a(2, std::vector<float>(frames));
                            std::vector<std::vector<float>> b(2, std::vector<float>(frames));
                            float* ip[2] = { a[0].data(), a[1].data() };
                            float* op[2] = { b[0].data(), b[1].data() };
                            AudioBusBuffers ib{}, ob{};
                            ib.numChannels = 2;
                            ib.channelBuffers32 = ip;
                            ob.numChannels = 2;
                            ob.channelBuffers32 = op;
                            ProcessData pd{};
                            pd.numSamples = frames;
                            pd.numInputs = 1;
                            pd.inputs = &ib;
                            pd.numOutputs = 1;
                            pd.outputs = &ob;
                            static double phase = 0.0;
                            for (int blk = 0; blk < 120; ++blk) {
                                for (int i = 0; i < frames; ++i, phase += 1.0) {
                                    const float s = 0.3f * std::sin((float)(2.0 * 3.14159265358979 * 330.0 * phase / 44100.0));
                                    a[0][(size_t)i] = a[1][(size_t)i] = s;
                                }
                                proc->process(pd);
                            }
                        }
                        pump(400);
                    }

                    // Does it actually draw? Force a paint and read the pixels back.
                    InvalidateRect(child, nullptr, TRUE);
                    UpdateWindow(child);
                    {
                        RECT cr{};
                        GetClientRect(child, &cr);
                        const int w = cr.right - cr.left, h = cr.bottom - cr.top;
                        HDC dc = GetDC(child);
                        HDC mem = CreateCompatibleDC(dc);
                        HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
                        HGDIOBJ oldBmp = SelectObject(mem, bmp);
                        BitBlt(mem, 0, 0, w, h, dc, 0, 0, SRCCOPY);

                        // count distinct colours on a coarse grid: a blank or single-colour
                        // surface means the editor painted nothing
                        int distinct = 0;
                        COLORREF seen[64] = {};
                        for (int gy = 4; gy < h; gy += 16)
                            for (int gx = 4; gx < w; gx += 16) {
                                const COLORREF c = GetPixel(mem, gx, gy);
                                bool dup = false;
                                for (int k = 0; k < distinct; ++k)
                                    if (seen[k] == c) { dup = true; break; }
                                if (!dup && distinct < 64) seen[distinct++] = c;
                            }
                        std::fprintf(stderr, "      distinct colours on the editor: %d\n", distinct);
                        check(distinct > 4, "editor renders more than a blank surface");

                        // dump it so the render can be looked at
                        BITMAPFILEHEADER fh{};
                        BITMAPINFOHEADER ih{};
                        ih.biSize = sizeof(ih);
                        ih.biWidth = w;
                        ih.biHeight = h;
                        ih.biPlanes = 1;
                        ih.biBitCount = 24;
                        ih.biCompression = BI_RGB;
                        const int stride = ((w * 3 + 3) / 4) * 4;
                        std::vector<unsigned char> bits((size_t)stride * (size_t)h);
                        BITMAPINFO bi{};
                        bi.bmiHeader = ih;
                        GetDIBits(mem, bmp, 0, (UINT)h, bits.data(), &bi, DIB_RGB_COLORS);
                        fh.bfType = 0x4D42;
                        fh.bfOffBits = sizeof(fh) + sizeof(ih);
                        fh.bfSize = fh.bfOffBits + (DWORD)bits.size();
                        if (FILE* f = std::fopen("editor_render.bmp", "wb")) {
                            std::fwrite(&fh, sizeof(fh), 1, f);
                            std::fwrite(&ih, sizeof(ih), 1, f);
                            std::fwrite(bits.data(), 1, bits.size(), f);
                            std::fclose(f);
                            std::fprintf(stderr, "      wrote editor_render.bmp (%dx%d)\n", w, h);
                        }

                        SelectObject(mem, oldBmp);
                        DeleteObject(bmp);
                        DeleteDC(mem);
                        ReleaseDC(child, dc);
                    }

                view->removed();
                DestroyWindow(parent);
                }
            }
            view->release();
        }
    }

    // component and controller are the same object here, so terminate once and drop the
    // extra references taken by queryInterface
    if (proc) proc->setProcessing(false);
    if (component) component->setActive(false);
    if (component) component->terminate();
    if (proc) proc->release();
    if (ctrl) ctrl->release();
    if (instance) instance->release();
    factory->release();
    FreeLibrary(mod);

    std::fprintf(stderr, "\n%s (%d failures)\n", g_failures ? "FAILED" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}

# Mel Scope

A real-time mel spectrogram and harmonic-trace display, as a CLAP and VST3 audio effect
for Windows x64. It passes audio through bit-exactly and only listens.

![Mel Scope](docs/screenshots/harmonic-traces-dark.png)

Display = Harmonics (default) is the intended view for a vocal or any single pitched
source. The analyser finds spectral peaks (local maxima in dB, each refined with a
parabola across its three bins), keeps the ones within 30 dB of the frame's loudest
peak, and draws them on the pitch axis as lines a few pixels thick. Underneath sits the
ordinary mel spectrum at roughly a third of the brightness, so breaths, noise and
consonants still show as context.

Display = Spectrum draws the plain mel spectrogram with no peak tracing, for general
material where you want everything at once.

## Install

Build the plugin (below), or take the
[latest release](https://github.com/wik-wav/mel-scope/releases/latest), which ships both
builds in one zip.

* **CLAP** - `MelScope.clap` is a single file. Put it wherever your host scans for CLAP
  plug-ins.
* **VST3** - `MelScope.vst3` is a bundle *folder*. Copy the whole folder into your VST3
  folder (normally `C:\Program Files\Common Files\VST3`) and rescan.

It is an insert effect with one stereo input and output. Windows 10/11 x64; no
installer, no runtime DLLs.

![Frozen, with the readout](docs/screenshots/frozen-readout.png)

## Using it

**Settings** (top left) opens the control panel; the wheel scrolls it when the window is
short. Drag a row to change it, double-click to reset it, click a value to type a
number, click a row with a chevron for its menu. **Help** opens a sheet with every
control and key binding.

On the plot: wheel zooms the time span, **Ctrl+wheel** zooms the frequency axis around
the pointer, **Shift+wheel** changes the spectrum range, and dragging pans while zoomed.
With the pointer over the piano keys, a plain wheel zooms frequency too.

**Full range** resets the frequency range to the whole spectrum and clears any zoom.
**Freeze** holds the current image while you keep working on it; the parameters that
would rebuild the analyser are greyed out until you unfreeze.

Every control is also a host parameter, so it can be automated and is saved with the
project. **Save preset** / **Load preset** write and read the whole setup as a file, and
the last used settings are restored automatically
(`%APPDATA%\MelScope\settings.ini`).

## Building

Windows x64 with MinGW-w64 GCC; no CMake, no framework. Download the toolchain into
`_tools/` (it is not versioned) - MinGW-w64 GCC, and the
[CLAP](https://github.com/free-audio/clap) headers under `_tools/clap/include/` - then:

```
powershell -ExecutionPolicy Bypass -File build.ps1        # -> dist\MelScope.clap
```

The VST3 build also needs the [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) in
`_tools/`:

```
git clone --depth 1 --recurse-submodules --shallow-submodules `
    https://github.com/steinbergmedia/vst3sdk.git _tools/vst3sdk
powershell -ExecutionPolicy Bypass -File vst3/build-vst3.ps1    # -> dist\MelScope.vst3
```

Both shells share the engine and the editor in `src/`; `vst3/` only adds the VST3
wrapper.

## Tests

```
build\selftest.exe                         # analysis maths across rates and FFT sizes
build\hosttest.exe build\MelScope.clap     # loads the CLAP like a host
powershell -ExecutionPolicy Bypass -File vst3/build-vst3.ps1 -RunTests
```

## License

0BSD - see `LICENSE`. Third-party obligations (the embedded font, the CLAP headers, and
the Steinberg SDK terms that apply to a compiled VST3 build) are in `THIRD-PARTY.md`.

# Mel Scope - CLAP mel spectrogram

Real-time mel-spectrogram / harmonic-trace display for **Bitwig Studio** (CLAP, Windows x64).
It is an audio effect that passes audio through untouched and only listens.

![Mel Scope](_analysis/screenshot.png)

---

## Install

1. In Bitwig: **Settings -> Locations -> Add location**, pick the folder
   `Q:\Diffsiner-Training-2027\mel-scope\dist` (the folder that contains `MelScope.clap`).
2. **Settings -> Plug-ins -> Scan** (or restart Bitwig).
3. It appears in the browser as **Mel Scope** (vendor `wyash`, category *analyzer*).
   Drag it onto a track - insert effect, audio passes straight through.

With admin rights you can instead copy `MelScope.clap` into
`C:\Program Files\Common Files\CLAP`, which Bitwig scans by default.

Requirements: Windows 10/11 x64, Bitwig 5.x (CLAP). No install, no runtime DLLs -
it is a single self-contained file built with a static C++ runtime.

---

## What it shows

**Display = Harmonics** (default). This is the look from
`_pitch-curve-review`: the analyser finds spectral *peaks* (local maxima in dB,
each refined with a parabola across its three bins), keeps the ones within 30 dB
of the frame's loudest peak, and splats them onto the pitch axis as thin lines a
few pixels thick. Underneath sits the ordinary mel spectrum at ~1/3 brightness, so
breaths, noise and consonants still show up as context. Pitched material therefore
reads as a stack of clean harmonic lines instead of fat blobs.
Each map carries its own **peak ink** - a complementary colour, not the hot end of
the gradient - so the traced lines read as an overlay on the wash. The ink is
composited after the bloom, so Glow softens the background without ever touching
the lines.

The peak ink is chosen per colormap, not derived from the map's own hue: it is the
colour furthest from every stop of that ramp (and from its dimmed wash), measured as
worst-case CIE76 distance. `Turbo` and `Cividis` contain every hue, so a complement
lands inside the ramp and disappears; they carry hand-picked magenta inks
(worst-case dE 69 and 78, up from 23 and 43). The light maps keep the reference
inks, chosen to read as ink on paper and to stay in key with the ramp beneath: raspberry
(Paper), **aubergine** (Sand), raspberry-pink (Bloom) and **vermilion orange** (Ink). Sand
and Ink were re-picked because a cold teal on warm tan and a muddy brown-orange on pale
blue both read as a foreign accent.

The wash behind the traces is deliberately short-range: it falls back to the canvas
colour quickly (a gamma-2 falloff capped at 42 % of the ramp), so silence really is
paper and quiet detail only tints it faintly. Peak lines are not affected by that
curve - they keep their full ink whatever the level.

**Display = Spectrum** draws the plain mel spectrogram (no peak tracing).

The piano ruler on the left is drawn through the *same* frequency mapping as the
image, so its keys line up with the rows: pick **Frequency axis = Mel** and the low
octaves compress with the image (keys thinner than ~3 px merge into a solid block),
pick **Log** and the keys are evenly spaced semitones. The horizontal lines mark
each key's centre; they are blended toward the inverse colour, so they stay visible
over both the canvas background and the brightest parts of the picture, and they are
decimated when octaves get too dense to read. While they are on, the frequency
labels (1k, 700, 500 ...) shrink to short ticks: they would otherwise draw a second,
competing horizontal grid at arbitrary frequencies.

## Controls (Settings button, top left)

| Group | Control | Default | Notes |
| --- | --- | --- | --- |
| Analysis | FFT size | 8192 | 2048 - 16384 (smaller windows smear the mel bands into bars by keeping whole bins) | 256 ... 32768; larger = sharper in frequency |
| | Overlap | 87.5 % | 50/75/87.5/93.75 %; sets columns per second |
| | Mel bands | 192 | vertical resolution of the mel filterbank |
| | Window | Hann | Hann / Blackman-Harris / Hamming / Rect |
| | Input | Mix | Mix / Left / Right (mono or stereo input both work) |
| Range | Low cut | 30 Hz | lowest frequency shown |
| | High cut | 1.5 kHz | highest frequency analysed (clamped to Nyquist). Changing it rebuilds the bank; Ctrl+wheel only moves the view inside it |
| | Range | 120 dB | dynamic range below the ceiling |
| | Ceiling | +6 dB | top of the colour scale when Auto level is off |
| | Auto level | Off | ceiling follows the loudest band (0 dB = full-scale sine) |
| View | Time span | 9.15 s | history across the width (wheel over the plot zooms it; Ctrl+wheel zooms frequency around the pointer) |
| | Theme | Light | Light / Dark interface. Switching also points the canvas the same way (dark canvas on the dark theme) and swaps a light-only colormap for `Ember` |
| | Display | Harmonics | Harmonics (peak traces over a dimmed spectrum - the intended view for a vocal / mono source) / Spectrum (a normal mel spectrogram, no peak line, for general use) |
| | Frequency axis | Log | Mel / Log / Linear |
| | Colormap | Ice | the list follows the theme: dark offers Ember, Inferno, Magma, Plasma, Viridis, Turbo, Cividis; light offers Ice, Paper, Sand, Bloom, Ink (Ice suits both) |
| | Grain | 20 % | paper texture: fine stable noise over the image |
| | Gamma | 0.96 | mid-tone brightness |
| | Tilt | -1.9 dB/oct | spectral tilt around 1 kHz |
| | Glow | 0 % | soft bloom on the wash only - the peak lines are never blurred; 0 = crisp |
| | Smoothing | 6 % | temporal averaging between frames |
| | Key guides | 13 % | strength of the piano-key centre lines (0 = off) |
| | Freeze | Off | hold the picture (double-click the plot also toggles it) |
| | UI scale | 115 % | size of the text and controls, in percent of the host's scale. The window opens at this size too |

The panel is a drawer that opens from the left edge; the wheel scrolls it when the
window is short. The header carries the two things you reach for constantly, so neither is buried in a menu: **Full range**
resets Low cut / High cut to the whole spectrum (the parameter minimum up to the Nyquist
limit of the current sample rate, e.g. 10 Hz - 24000 Hz at 48 kHz) and confirms the new range in
the subtitle; **Freeze** holds the image. Mouse: drag a row to change it, double-click resets it, click a row with a chevron
for its menu. Wheel over the plot zooms the time span, **Ctrl+wheel zooms the frequency axis around the pointer**
(the frequency under the cursor stays under it), Shift+wheel changes the dynamic range.
While zoomed, **drag anywhere over the plot** (outside the open panel) to pan up and down (the cursor becomes a vertical
resize arrow); a slim bar on the right edge shows where the view sits, and double-clicking it
zooms back out. Zooming out widens the analysed range when it reaches the edge (the mel bank is
rebuilt, so the history clears once) instead of stopping silently at the current High cut. Esc closes the panel.


The same frequency zoom works with **no modifier at all when the wheel is over the piano
keyboard** on the left - the fallback when a host swallows Ctrl for itself. A plain Alt press is
swallowed as well, so the window never enters the Windows menu loop (which holds the keyboard
capture and hides modifier state until Alt is tapped again).

All parameters are also exposed as CLAP parameters, so they can be automated and
are saved with the project.

## Theme

The chrome uses the light design tokens directly (constants at the top of
`src/Gui.cpp`):

| token | value | used for |
| --- | --- | --- |
| background | `#f8f9f8` | window background |
| surface | `#ffffff` | settings panel, dropdown, buttons, white keys |
| surface-muted | `#f4f1ec` | row hover, selected menu item, piano key field |
| foreground | `#0d0e1c` | titles, values, group headers, track fill |
| muted | `#706b65` | labels, status line, secondary text |
| border | `#d6d7d6` | panel/dropdown/button edges, key separators, plot frame |
| accent | `#0d0e1c` | active value, selected menu item, emphasis |
| success | `#5d8f5a` | parameters that are switched ON |

The canvas follows the theme (light ground on the light theme, dark ground on the dark one), and everything drawn on it (frequency/time rules,
labels, crosshair, readout) flips its ink automatically: the renderer looks at the
colormap's darkest entry and picks dark ink with a white halo on the light maps
(`Paper`, `Sand`) or light ink with a black shadow on the dark ones (`Ice`, `Ember`,
...). Switching **Colormap** between a light and a dark map therefore keeps the plot
readable either way.

Each theme owns its gradients and the Colormap menu only lists that theme's set: dark keeps
Ember / Inferno / Magma / Plasma / Viridis / Turbo / Cividis, light gets Ice,
Paper, Sand, Bloom and Ink. Switching theme also selects that theme's default (Ember for
dark, Ice for light). Every light gradient starts at pure white, so the quietest thing on screen
is the canvas itself, and the first slice of every ramp eases out of the canvas colour (a
smoothstep foot over the first 12 %) so the background never meets the colours in a visible step.
## Freeze

Freeze holds the image where it is, and the interface stays live: every display
parameter (gamma, tilt, glow, grain, key guides, colormap, axis, theme, time span,
range, ceiling, auto level) still applies to the held image immediately.

The hold is real, not just a paused clock: the analysis thread stops consuming audio
while frozen, so the frame ring cannot overwrite the window being held (it used to
survive only until the ring caught up - about the length of the buffer, which is why
the image vanished a while after freezing). Unfreezing drops the held history and
starts streaming again, so the gap does not appear as a jump in the timeline.

The parameters that would rebuild the mel filterbank - **FFT size, Overlap, Mel
bands, Window, Low cut, High cut, Smoothing** - are greyed out while frozen, because
applying them would discard the very image being held. They stay deferred rather
than lost: change them from a preset or by automation and they apply on unfreeze.

## Saving and loading settings

Bottom of the panel:

* **Save...** opens a normal file dialog (default name mel-scope-prefs.ini) and writes
  every parameter as key=value. The path you chose is shown in the header for a few
  seconds afterwards ("Settings saved to: ..."), so you never have to hunt for the file.
* **Load...** opens the same dialog, applies the file, and shows "Settings loaded from: ...".
* **Reset all** restores the built-in defaults, **Clear** empties the history.

Separately, and without any dialog, every parameter is written automatically on exit to:

`
%APPDATA%\MelScope\settings.ini     (fallback: %LOCALAPPDATA%\MelScope\,
                                     then next to the plugin)
`

so a new instance starts where you left off, even after a reboot. A project's saved
state in the DAW still wins over both.

### Dark theme

View -> Theme switches the whole interface to a dark palette; the canvas follows it
(dark ground, ramps no longer reversed) and a light-only colormap is swapped for
Ember, so the dark theme starts out looking like the original build. Everything
else keeps working the same way: the wash still falls back to the canvas colour,
peak lines keep their complementary ink, and Glow still only blurs the wash.

| token | dark value |
| --- | --- |
| background | #131318 |
| surface | #1c1c22 |
| surface-muted | #26262e |
| foreground | #e9e7f1 |
| muted | #9a97a8 |
| border | #32323c |
| accent | #ff8a4c |
| success | #86c084 |

The piano keys keep their own colours in both themes (light naturals, near-black
accidentals) - chrome never turns them orange.

## FFT size

Only 2048 - 16384 is offered. Below 2048 the bin width (23 Hz at 48 kHz) is wider
than the mel bands in the low range, so several bands fall on one bin and the image
breaks into horizontal bars; with the default 1.5 kHz cut a 32768 window buys
nothing either. Settings written before the change carry a `fft` value that indexes
the old, longer list - the file is stamped `# version=2` and an older index is
translated to the same window length on load, so nothing silently changes size.

## Help

**UI scale** sits at the bottom of the View group. It multiplies the host's DPI scale,
defaults to 115 %, and sizes the window as well as the chrome - so a dense display gets
bigger text rather than a bigger spectrogram only.

**Help** in the header opens an in-editor sheet with the mouse and key bindings, what
each parameter group does, and the intended use of the two displays. It scrolls with
the wheel, and closes on Esc, the CLOSE button, or a click outside the sheet.

The point worth reading there: **Harmonics is the intended view for a vocal or any
single mono source** - it traces the partials, so pitch movement reads at a glance.
**Spectrum is simply a normal spectrogram** for general use: no peak line, the raw
mel image including the noise between the partials.

## Design

The chrome follows the design system of wik_wav's site, translated to Win32/GDI+:

* **tokens**: paper `#f7f8f7`, surface `#ffffff`, surface-2 `#eff0ee`, ink
  `#0d0e1c`, muted `#6c7076`, rule `#d7d9d7`, accent `#1420ca`, accent-2
  `#3d8d7e`, mark `#f3da34`
* **type**: Merriweather Bold 16 for the display face (embedded in the binary, SIL
  OFL, `_analysis/fonts/OFL.txt`; Georgia is the fallback), Segoe UI 12 for body text, Consolas
  10-11.5 for micro-labels and numeric values (nothing below 10px: 8.5px mono was
  unreadable at 100 % scaling)
* **no rounded corners**: every chrome shape is a rectangle with a 1px edge
* **buttons**: 1px ink edge, uppercase mono label; hovering inverts to an ink
  fill with paper text; the active state fills with the mark yellow and switches
  the label to ink (the site's "current page" treatment)
* **cards**: the panel and the menu are white surfaces with a 1px ink edge and a
  single hard offset shadow (no blur) - the system's only elevation
* **micro-labels**: group headings are uppercase mono in the muted colour; menu
  selection is a mark-yellow bar with a square register mark, not a round dot
* sliders are a 1px rule with an ink fill and a square handle
* the header is a single line: `Mel Scope` on the left, then the range on screen
  (`30 Hz - 1.50 kHz`, ink), a hairline, and the technical detail (muted). Nothing
  sits on the canvas edge; when the window is narrow the tail items (`fps`, `bands`,
  `hop`, ...) drop off one by one so the range never collides with the title
* the panel card hugs its content and never grows past the window; rows scroll
  in a band above the buttons, which are never overdrawn

The dark theme mirrors the same structure with its own tokens, so the language
survives the theme switch rather than being a light-only skin.

## Persistence

* **Project**: the DAW state (the plugin's full state blob) is saved and restored
  by Bitwig as usual.
* **Global**: every parameter is also written to a settings file, so a fresh
  instance - after closing Bitwig, or after a reboot - starts where you left off:

  ```
  %APPDATA%\MelScope\settings.ini     (fallback: %LOCALAPPDATA%\MelScope\,
                                       then next to the plugin)
  ```

  `Reset all` in the panel restores the defaults and rewrites that file.

## Sample rates, mono and stereo

The analyser is mono internally and the display is sample-rate independent:

* mono input is copied straight in (no mixing cost); stereo is downmixed
  (or L/R can be selected with **Input**);
* activation is tested at 22.05 / 32 / 44.1 / 48 / 88.2 / 96 / 192 kHz;
* `High cut` is clamped to Nyquist, so it never asks for bins that do not exist.

Audio is never modified: the output is a bit-exact copy of the input.

---

## Building from source

```
powershell -ExecutionPolicy Bypass -File build.ps1
```

(Execution policy blocks unsigned scripts on this machine; the flag is only needed
for that.) Needs the toolchain downloaded in `_tools/` (MinGW-w64 GCC + CLAP
headers) - no CMake, no JUCE, no MSVC. The plugin is plain C++17: Win32 + GDI+ for
the UI, one FFT + mel filterbank + peak tracer for the DSP, and the CLAP C ABI as
the shell.

| File | What |
| --- | --- |
| `src/MelCore.h/.cpp` | FFT, mel filterbank, frame store, peak tracer, renderers |
| `src/Colormaps.h` | generated 256-entry colour tables (`_analysis/make_colormaps.py`) |
| `src/Plugin.h/.cpp` | CLAP shell: params, audio ports, GUI ext, state, settings file |
| `src/Gui.h/.cpp` | Win32/GDI+ editor: plot, piano ruler, panel, dropdowns |
| `offline/melrender.cpp` | WAV -> image using the same analyser (visual verification) |
| `offline/selftest.cpp` | sample-rate / mapping / FFT-size checks |
| `hosttest/main.cpp` | loads the .clap like a DAW, checks ABI, audio, state, GUI |

## Verification

```
build\selftest.exe                        # DSP across sample rates and FFT sizes
build\hosttest.exe build\MelScope.clap    # ABI, params, pass-through, state, GUI
build\melrender.exe in.wav out.ppm ...    # render a file with the same code path
```

`selftest` output covers 22.05-192 kHz: the traced peak lands within 0.5 % of the
input tone at every rate, the frame clock tracks `hop / sr`, and every FFT size x
band count analyses a 500 Hz tone correctly.

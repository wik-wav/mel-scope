# Third-party notices

Mel Scope's own code is **0BSD** (see `LICENSE`). Three kinds of third-party material
matter when you build, redistribute or bundle it.

## CLAP (the CLAP build)

The CLAP build compiles against the CLAP headers, which are MIT licensed:

```
CLAP -- https://github.com/free-audio/clap
Copyright (c) Alexandre Bique and contributors
MIT License
```

MIT requires that its notice travel with copies of the software, so keep this file (and
the CLAP licence text, if you vendor the headers) alongside a redistribution.

## Merriweather (embedded font)

`src/Fonts.h` embeds a compressed Merriweather Bold TTF so the editor can draw its
display face without shipping a font file. Merriweather is licensed under the SIL Open
Font License 1.1 (`licences/OFL-1.1.txt`), which allows embedding and redistribution,
including inside a binary, provided the licence travels with it — this file plus
`licences/OFL-1.1.txt` are that copy. The font is not sold on its own here, and no
reserved font name is reused by a derivative.

## VST3 SDK (the VST3 build)

**A compiled VST3 build is not 0BSD.** `vst3/build-vst3.ps1` compiles several files from
the Steinberg VST3 SDK into `MelScope.vst3` (its interface headers, the single-component
effect base, the plug-in factory, and the Windows module entry). That SDK is dual
licensed — GPLv3, or a proprietary licence from Steinberg — so:

* **distributing a compiled VST3 build** puts the SDK's terms on that binary: either the
  GPLv3 (which would pull the combined work under the GPLv3) or a Steinberg licence;
* the 0BSD grant applies to this repository's own source, and to the CLAP build.

If you only ship the CLAP build, none of this applies: it depends on nothing but the
platform SDKs and the MIT CLAP headers.

The SDK itself is deliberately **not** vendored here — `vst3/build-vst3.ps1` expects it
in `_tools/vst3sdk` (see the README), which keeps this repository free of SDK sources.

## Platform SDKs

Windows: Win32 and GDI+, used through the MinGW-w64 import libraries. No redistribution
obligation beyond what the OS licence already covers.

# Third-party software

HeartGold PC Remastered is distributed under GPL-3.0-or-later. Its source and
binary distribution depend on the following projects:

| Component | Version | Upstream | License |
| --- | --- | --- | --- |
| Qt | 6.11.2 | https://www.qt.io/ | LGPL-3.0-only, GPL-3.0-only, or commercial |
| SDL | 3.4.16 | https://github.com/libsdl-org/SDL | Zlib |
| melonDS | 1.1 | https://github.com/melonDS-emu/melonDS | GPL-3.0-or-later |

Qt is used under an applicable Qt license. SDL is used under the zlib license.
The distributed application links melonDS; project code is therefore licensed
under GPL-3.0-or-later.

melonDS 1.1 is pinned as the `third_party/melonds` submodule at
`b86390e4428bf38ce4c1ce0e9ca446d6d25955e8`. Clone with `--recurse-submodules`
or run `git submodule update --init` before configuring a fresh checkout.
Its original licenses and copyright notices remain in the submodule.

`src/emulation/melonds/MelonPlatform.cpp` adapts the platform contract and
stdio/dynamic-library wrappers from upstream `src/Platform.h` and
`src/frontend/qt_sdl/Platform.cpp` at that revision. The melonDS GPL header
and attribution are retained. Our context, worker, synchronization, logging,
and disabled-device implementations do not use the upstream frontend or its
globals. Pixel interpretation follows upstream `src/GPU2D_Soft.cpp` (BGRA bytes
in the software framebuffer, numeric AARRGGBB on the target platform).

The `heartgold_melonds` target alone links upstream `core`, privately. Core is
built as C++17; project targets remain C++20. Qt/SDL frontend, OpenGL renderer,
GDB stub, and LTO are disabled, with the x64 JIT enabled. The pinned core
unconditionally includes its DSi/DSP objects and vendored Teakra library even
for an NDS-only consumer. On Windows its CMake also requires `ole32`, `comctl32`,
`wsock32`, and `ws2_32`; the enabled JIT requires `onecore` for Windows memory
mapping. These are upstream core dependencies, not frontend dependencies.
Teakra retains its upstream MIT license under `src/teakra/LICENSE`.

Automated execution uses melonDS FreeBIOS and generated blank NDS firmware.
Loader tests generate zero-logo synthetic headers and never execute them.
No ROM, private BIOS, firmware dump, or other Nintendo assets are distributed.

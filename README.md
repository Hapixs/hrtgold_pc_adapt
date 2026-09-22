# HeartGold PC Remastered

Windows 11 x64 prototype for a modern PC presentation of a user-supplied
Pokémon HeartGold ROM. The application never includes, copies, or distributes
ROMs, BIOS files, firmware, saves, or extracted game assets.

## Prerequisites

Run the bootstrap script once to install the MSYS2 UCRT64 toolchain and declared
development dependencies:

```powershell
./scripts/bootstrap.ps1
```

## Build and test

The canonical debug build command configures, builds, and runs the test suite:

```powershell
./scripts/build.ps1 -Preset ucrt-debug
```

The release preset is also available:

```powershell
./scripts/build.ps1 -Preset ucrt-release
```

Project code is licensed under GPL-3.0-or-later. See
[`LICENSES/GPL-3.0-or-later.txt`](LICENSES/GPL-3.0-or-later.txt) and
[`THIRD_PARTY.md`](THIRD_PARTY.md).

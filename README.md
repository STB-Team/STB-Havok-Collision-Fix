# STB Havok Collision Fix

SKSE plugin for **Skyrim SE 1.5.97**. The game stops crashing or freezing on broken collision in modded meshes.
Open source (GPL-3.0-or-later). No ESP, nothing written to saves, safe to add or remove at any time.

## What it fixes

| Fix (ini key) | Engine bug | Symptom without the fix |
|---|---|---|
| `HitSort` | Havok sorts collision hits by distance with a quicksort (`sort_points`, REL::ID 42697) whose right-hand scan is `comiss pivot, [hit]; jb`. An unordered compare counts as "less", so a hit with a **NaN** distance picked as the pivot sends the scan below the start of the array. | Crash when aiming at, activating or walking near a broken object, or in an explosion. |
| `MoppAtLoad` | MOPP is the bytecode of a collision BV-tree, run by a tiny VM (REL::ID 66294) with no checks: an unknown opcode hits `__debugbreak()`, a jump can leave the buffer, an absolute jump can point back and loop forever. | `EXCEPTION_BREAKPOINT` crash with nothing useful in the crash log; crash or a phantom collision spike; instant freeze. |
| `CompressedMeshKeys` | `hkpCompressedMeshShape::getChildShape` (REL::ID 78721) trusts the shape key from the MOPP tree and indexes chunk, triangle and vertex arrays with it. | Crash or a "phantom" triangle built from random memory. |

How each fix works:

- **HitSort** — before every call of `sort_points` (all four call sites) NaN distances are replaced with `FLT_MAX`,
  so such hits sort last. Valid hits are not touched, the original sort runs as usual.
- **MoppAtLoad** — when a `.nif` is loaded, right after `bhkMoppBvTreeShape` reads its MOPP and before physics can
  use it, the bytecode is walked once with the engine's own decoding rules. Every instruction that would crash or
  hang the VM becomes `RETURN`: that branch of the tree is dropped, the rest of the mesh keeps its collision. If a
  tree cannot be repaired locally, the whole tree is disabled (no collision instead of a crash). The repaired mesh
  is named in the log. Costs nothing during gameplay: once per mesh load, on the loader thread.
- **CompressedMeshKeys** — the vtable slot of `getChildShape` checks every array access the engine is about to
  make for that key. An invalid key is replaced with the key of the first valid triangle of the same mesh (real
  geometry), then the engine function runs as usual. A few compares per call.

Tested on a 800-mod load order: 12 576 MOPP buffers (vanilla, USSEP, all mods) — no false positives;
4.6 million real shape keys of 12 419 compressed meshes — all accepted; 12 000 randomly corrupted MOPP buffers —
all repaired. Hook sites are verified byte by byte at startup; a site already patched by another mod is skipped.

Not covered: MOPP inside `.hkx` files (ragdolls; none broken found in vanilla), shape keys of non-compressed mesh
shapes, Skyrim AE / VR.

## Install

Requirements: [SKSE64](https://skse.silverlock.org/) for 1.5.97 and
[Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444).

Put `SKSE/Plugins/STBHavokCollisionFix.dll` (and optionally `STBHavokCollisionFix.ini`) into `Data`. Every fix can
be switched off in the ini (`[Fixes]`, `0` = off; a missing file or key means on).

Log: `Documents/My Games/Skyrim Special Edition/SKSE/STBHavokCollisionFix.log`. A quiet log means nothing needed
fixing; `warn` lines name what was caught (for `MoppAtLoad` — the `.nif` path, so the mesh can be fixed at the
source).

Do not combine with other plugins that patch the same functions (e.g. CollisionSentinel): it works, both just
check the same data twice.

## Build

Visual Studio 2026 (v145) with vcpkg (`VCPKG_ROOT`), CMake 3.21+.

```
git clone --recursive https://github.com/STB-Team/STB-Havok-Collision-Fix
cmake --preset default-2026
cmake --build build --config Release
```

Output: `build/Release/STBHavokCollisionFix.dll`. `build/Release/moppcheck.exe` runs the MOPP validator tests
without the game; `moppcheck <dump.bin>` checks real meshes dumped by `tools/extract_mopp.py`.

CommonLibSSE-NG (MIT) is a submodule in `external/` with a small patch (`external/patches/`), applied at configure
time.

## Credits and license

Engine analysis and code: STB Team (Skyrim True Believer). The crash classes were first described publicly by the
closed-source CollisionSentinel mod; this is an independent implementation from our own reverse engineering of
SkyrimSE.exe 1.5.97, written to have an open, auditable fix.

Copyright (C) 2026 STB Team. Licensed under the GNU General Public License v3.0 or later, see [LICENSE](LICENSE).

# STB Havok Collision Fix

SKSE plugin for **Skyrim SE 1.5.97**. The game stops crashing or freezing on broken collision in modded meshes and
on bookkeeping errors of the physics engine (Havok) itself: fights with many creatures, ragdolls, objects loaded and
unloaded around the player.
Open source (GPL-3.0-or-later). No ESP, nothing written to saves, safe to add or remove at any time.

## What it fixes

| Fix (ini key) | Engine bug | Symptom without the fix |
|---|---|---|
| `HitSort` | Havok sorts collision hits by distance with a quicksort (`sort_points`, REL::ID 42697) whose right-hand scan is `comiss pivot, [hit]; jb`. An unordered compare counts as "less", so a hit with a **NaN** distance picked as the pivot sends the scan below the start of the array. | Crash when aiming at, activating or walking near a broken object, or in an explosion. |
| `MoppAtLoad` | MOPP is the bytecode of a collision BV-tree, run by a tiny VM (REL::ID 66294) with no checks: an unknown opcode hits `__debugbreak()`, a jump can leave the buffer, an absolute jump can point back and loop forever. | `EXCEPTION_BREAKPOINT` crash with nothing useful in the crash log; crash or a phantom collision spike; instant freeze. |
| `CompressedMeshKeys` | `hkpCompressedMeshShape::getChildShape` (REL::ID 78721) trusts the shape key from the MOPP tree and indexes chunk, triangle and vertex arrays with it. | Crash or a "phantom" triangle built from random memory. |
| `SolverBuffer` | The solver of an island (group of touching bodies, REL::ID 61420) sizes its buffers from running totals of the island's constraints (`m_constraintInfo`) that can drift below what the constraints really write. | `EXCEPTION_BREAKPOINT` or access violation inside Havok in fights, especially with many creatures or ragdolls. |
| `IslandUnlock` | After solving, the simulation reads a field of the island only to pass it to `unlockIslandForConstraints` (60526), which ignores it; the island may already be gone. | Crash right after the solver. |
| `ListenerRemove` | Havok's `remove*Listener` functions (world, entity, phantom, constraint — 11 of them) use index −1 when the listener is not attached: they write before the array or shrink it wrongly. | Memory corruption: crash now or much later at a random place. |
| `PendingQueue` | Queued world changes are executed even while the world is locked for phantoms; a phantom operation then queues itself again and the queue re-runs itself (REL::ID 60585 → 61313). | Stack overflow crash. |
| `WorldLock` | The world's "busy" counters (`criticalOperationsLockCount`, `…ForPhantoms`) can be left negative or stuck by an unbalanced lock/unlock elsewhere. | Changes applied in the middle of the physics update, or never — random crashes later. |
| `BatchCheck` | Batch add/remove of bodies and phantoms (60494, 60495, 60503, 60505 — ragdolls, cells) does not check that an object is not yet in a world / is in this one / is listed once (debug asserts only). | Object freed while in use, broadphase mixed up — crash later at a random place. |
| `FootIkReserve` | The foot IK raycast list (`BGSFootIkRaycastInterfaceDB`, created for 50 entries) is reallocated when more characters need it, while others still use it. | Crash in foot placement with many characters around. |

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
- **SolverBuffer** — for the duration of the solver call the island's two totals get a reserve (schemas: doubled
  plus 1 KB; temps: doubled plus 16), then it is subtracted again. The buffers come from Havok's per-thread LIFO
  memory; the physics result is the same.
- **IslandUnlock** — the 7-byte load becomes `xor edx, edx` at all three call sites.
- **ListenerRemove** — "not found" returns without touching anything (`ret` in place of `or r9d, -1`; the one
  function with a stack frame is replaced as a whole).
- **PendingQueue** — while the phantom lock is held the queue is left for the next attempt (at the latest the next
  physics step); the queue's own recursion is limited to depth 64 / 64 KB of free stack as a last resort.
- **WorldLock** — at the start of every physics step, right after the step takes the world's write lock: a negative
  counter is set to 0, a counter stuck at the same positive value for 600 steps (~10 s) while changes wait is reset.
  `bUseWorldLock:HAVOK` (on by default) is switched back on if an ini turned it off.
- **BatchCheck** — a clean list (the normal case) passes untouched; otherwise the batch runs with only the objects
  that pass the checks. Locked world: the batch is queued as before and checked when the queue runs it.
- **FootIkReserve** — the list is created with room for 1024 entries (~57 KB).

Tested on a 800-mod load order: 12 576 MOPP buffers (vanilla, USSEP, all mods) — no false positives;
4.6 million real shape keys of 12 419 compressed meshes — all accepted; 12 000 randomly corrupted MOPP buffers —
all repaired. Hook sites are verified byte by byte at startup; a site already patched by another mod is skipped.

Not covered: MOPP inside `.hkx` files (ragdolls; none broken found in vanilla), shape keys of non-compressed mesh
shapes, Skyrim AE / VR; physics objects that were freed while still attached somewhere (use-after-free) — guessing
whether an object is still alive from its vtable is left out on purpose.

The physics fixes follow the analysis of the closed-source Havok Solver Crash Fix (Nexus 194392); the code here is
written from the engine, see `Skyrim-RE/docs/HavokSolver_Guards_RE.md`.

## Install

Requirements: [SKSE64](https://skse.silverlock.org/) for 1.5.97 and
[Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444).

Put `SKSE/Plugins/STBHavokCollisionFix.dll` (and optionally `STBHavokCollisionFix.ini`) into `Data`. Every fix can
be switched off in the ini (`[Fixes]`, `0` = off; a missing file or key means on).

Log: `Documents/My Games/Skyrim Special Edition/SKSE/STBHavokCollisionFix.log`. A quiet log means nothing needed
fixing; `warn` lines name what was caught (for `MoppAtLoad` — the `.nif` path, so the mesh can be fixed at the
source).

Do not combine with other plugins that patch the same functions (e.g. CollisionSentinel, Havok Solver Crash Fix):
it works, but a place already patched by the other plugin is skipped here (`warn` in the log).

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

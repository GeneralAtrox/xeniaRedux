# XeniaRedux: can Xenia Canary check a Too Human port?

Date: 2026-10-22. Fork of `xenia-canary/xenia-canary`, branch `canary_experimental` at `e6cd2e4bc`
(2026-10-20). Local branch `redux-eval`. Pushing is disabled in this clone (the push address is set to
`DISABLED-never-push`). Nothing was pushed, and no issues or PRs were opened. Xenia is BSD-3 licensed. `LICENSE` is
kept, and the fork lives outside DecompPipeline.

## The short answer

**Yes, Xenia Canary can be the Xbox 360 reference, but not the way Flycast is used on Dreamcast.** It builds, and it
runs Too Human well. It is not repeatable, though: two runs from the same disc with the same settings do different
things. Flycast's frame-by-frame lockstep needs repeatable runs, so it cannot work on Xenia without a large change.

What works without that change is a **per-function** check. XeniaRedux records what the real game does in each
function call (what went in and what came out). The port's version of that function is then checked against those
recordings, and DP's judge settles any disagreement. Making whole runs repeatable is a later, larger step.

## 1. Build: yes, about 24 minutes in total

| Step | Time |
|---|---|
| Clone (one branch) | under 10 s |
| Third-party parts (33 submodules, shallow) | about 4 min, 1.4 GB |
| `xb setup` (CMake configure) | 1 min 54 s |
| Release build (8 compile jobs) | 7 min 07 s, plus a 1-min failed first try |

The toolchain was Visual Studio 2022 Community 17.14, CMake 4.3, Python 3.12 and Windows SDK 10.0.26100. RAM peaked
under the 95% stop line, so the RAM guard never paused the build. The result is
`build/bin/Windows/Release/xenia_canary.exe` (17 MB).

Two snags, both fixed locally:

1. **The build script could not find its own `vswhere.exe` on this machine.** Windows would not start it from a
   path written with `/`. One line in `xenia-build.py` was changed to use `\\`. This is the only source change, and
   it is committed on `redux-eval`.
2. **The Vulkan SDK is not installed.** Xenia needs two of its shader tools (`glslangValidator` and `spirv-opt`).
   MSYS2 already has both in `C:\msys64\mingw64\bin`, so the build used those (`VULKAN_SDK=C:\msys64\mingw64`), and
   nothing new was installed. Installing the real Vulkan SDK would be the tidier long-term fix. That is your call.

The build scripts are in `eval/`. `guarded_build.ps1` is the RAM-guarded, time-limited build.

## 2. Too Human boot: it boots and plays the opening, but no title screen without a controller

The disc image was read as it is, and no game files were changed. Each run had sound off, no controller, a time
limit, and a forced stop at the end. Snapshots were taken of Xenia's own window.

| Run | Length | What happened |
|---|---|---|
| 1 | 146 s | Boots in about 20 s, with shaders preloading. Logos, the Nietzsche quote (70 s), then the in-engine opening cutscene (Baldur, 100–130 s). |
| 2 | 422 s | **Stalled.** The Silicon Knights logo stayed on screen from 60 s to the end. The log shows loading stopped 5 files short of where runs 1 and 3 got to. |
| 3 | 146 s | Like run 1, but at 145 s it was at a **different shot** of the cutscene than run 1 was. |
| 4 | 543 s | Opening cutscene through to the "A Silicon Knights Game" credit at 300 s. At about 360 s the window was maximised, then minimised, and the title bar showed the game clock at 4096× speed (12 "double speed" steps). My script did none of that, so everything after 300 s is set aside. |

- **Title screen: not reached.** Nothing appeared in 300 s of unattended play. The opening cutscene needs a button
  press to skip, and Xenia cannot play back recorded button presses (see the table below).
- **Gameplay: not reached**, for the same reason. The cutscene is real-time, in-engine rendering, so the game's own
  code is running, not a video. The compatibility entry and the earlier reports agree that the game is playable with
  a controller.
- **Log health.** The only errors were two calls to an unimplemented kernel function, `IoDismountVolumeByFileHandle`,
  which Xenia skips. The game starts **about 40 guest threads**.
- **Repeatability, measured.** The order in which the game opened its files was the same in all three logs for the
  first 64 files, then differed (`menu.bnk`, `\Video`, `scene00_hologram_wmv.wmv` swap places). Run 2 then stalled.
  The file orders are in `eval/run*_file_order.txt`, and the snapshots are in `eval/*.png`.

## 3. What DP's Dreamcast finder relies on (for comparison)

From `port/tools/lockstep.py`, `divergence.py` and `ref_fast.py` on the `divergence` branch, and FlycastRedux's
`core/research/`:

- Flycast plays the game from a **scripted button list** (`START@900,A@1500+10`), keyed to the vertical blank (each
  screen refresh, called a "vblank" below).
- At each frame it writes the **RAM blocks that changed** (`--frame-pc`, RamFrames). It also saves a **save state
  every N vblanks**.
- Flycast is **deterministic**: the same input gives the same recording, so one recording serves every port build.
- The port replays those frames with its timer and buttons taken from the emulator's frames. Then the finder
  compares RAM word by word and names the first differing frame.
- **"window" step.** It replays from the nearest save state, with every write to one word recorded (PC, PR, value),
  to name the original's writer.
- **Block coverage** (`coverage.txt`), a Lua script layer and a control socket.

## 4. Hooks: what Xenia has, what is missing, effort to add

Effort: **small** means a day or two of agent work, **medium** several days, **large** weeks, with risk.

| Need | Xenia today | Missing | Effort |
|---|---|---|---|
| **Deterministic start** | Boots cleanly from the ISO every time. **No save states:** there is only an unfinished skeleton (`Emulator::SaveToFile`, F7/F8 in debug builds only, fixed name `test.sav`). Sound saves nothing but a marker, and many kernel object types are not saved. **Not repeatable:** the game's ~40 threads are real Windows threads, the game clock is the PC's wall clock (scaled by `time_scalar`), and vblanks come from a host timer thread. | A deterministic mode: one scheduler running guest threads in a fixed order, the clock and vblanks counted rather than timed, file reads finishing at fixed points. After that, save states (finish the skeleton). | **Large** (determinism); **large** (save states, and only useful once runs repeat) |
| **Input recording and replay** | None. The input driver interface is tiny (`InputDriver::GetState`), and keyboard, XInput, SDL and "nop" drivers exist. | A "replay" driver that reads DP's `--input` syntax keyed by vblank count, and a "record" wrapper. | **Small**, but it only gives repeatable runs once the deterministic mode exists |
| **Headless / no window** | `--headless` only answers dialog prompts automatically: a window is always created. A null GPU (`--gpu=null`), no sound (`--apu=nop`) and no input (`--hid=nop`) exist. The GPU trace-dump tools show that window-less running is possible. | A no-window launch path (skip `EmulatorWindow`, null presenter). | **Small–medium** |
| **Per-frame dumps of memory and registers** | None built in. Guest memory is one block the host can read directly. `GraphicsSystem::MarkVblank` is a natural per-frame hook. Each thread's registers sit in its `PPCContext`, and the debugger can pause all threads (`Processor::Pause`). | A vblank writer of changed RAM blocks in DP's `--ram-frames` format, plus a register dump of chosen threads. A "frame PC" trigger like Flycast's needs the entry hook below. | **Small** (RAM at vblank); **medium** (at a guest PC) |
| **Function-entry tracing by guest address** | Partly there. `--trace_functions`, `--trace_function_coverage` and `--trace_function_references` count calls per function, keep the last 4 callers and count each instruction's runs, all in a mapped file. These are **totals, not an ordered log**. `--break_on_instruction` only triggers a host debugger break (no use headless). The debugger's guest-address breakpoints (`Breakpoint`, `DebugListener::OnBreakpointHit`) work but are slow. | An ordered call log, emitted by the JIT at a function's start and end like the existing counters: thread, sequence number, argument registers (r3–r10, f1–f13), LR, and the return values. | **Medium** |
| **"Who wrote this word" (DP's window step)** | Only a compile-time switch (`DTRACE` in `x64_tracers.cc`) that logs **every** load and store as text, which is far too slow for a whole game. Page-protection access callbacks exist (`Memory::EnableAccessCallbacks`, used by the GPU). | A filtered store watch: a few addresses, logged with PC, LR, value and vblank. | **Medium** |
| **Code coverage** | Yes, as per-instruction counts (`--trace_function_coverage`). | A converter to DP's `coverage.txt`. | **Small** |
| **CPU backend choices** | **JIT only**: x64 (this build) and ARM64. The "null" backend runs nothing. **There is no interpreter.** There is a direct one-function call (`Processor::Execute(thread, address, args)`) and an instruction test runner (`xenia-cpu-ppc-tests`, 571 test files). | An interpreter would be a separate project. | **Large** (not recommended) |
| **Scripting / control socket** (Flycast has Lua and a socket) | None. | A small command pipe (pause, dump, quit) if needed. | **Medium** |
| **Occlusion-query results** | Four modes: `fake`, `fast` (the default: one frame late, guessed), `fast-alt` and `strict` (real counts, with a 2 ms wait limit). | A log of the numbers handed to the game, so the port can be fed the same ones. | **Small** |

## 5. Accuracy risks for Too Human

The compatibility entry (game-compatibility#736) has these labels: `state-gameplay`, `cpu-jit-problems` and
`gpu-occlusion-query`. For each risk below, the key question is: when the port and Xenia disagree there, **whose
fault is it?**

**a. `cpu-jit-problems`.** The label was added on 2026-03-30 with no details. The source shows exact places where
Xenia's JIT is known to differ from the real Xenon CPU. A difference inside a function using these is **likely Xenia's
fault** whenever the port follows the hardware:

- `fres`, `frsqrte`, `vrefp` and `vrsqrtefp` (the "estimate" instructions) are computed **exactly**. The real chip
  gives table estimates. Xenia's own hardware-made tests show `fres(1.0) = 0.99609375`, not `1.0`. DP already refuses
  these functions, so this is not a problem today.
- **Host-dependent results.** Some answers depend on the PC that runs Xenia, which a reference must never do:
  - `frsqrte` (single) uses a different approximation on AVX-512 CPUs;
  - `fmadd` is fused only if the PC's CPU has FMA (any modern one does); otherwise it rounds twice.
- `stfs` of a double that is out of single range **rounds**. The hardware **selects bits** instead; DP's runtime
  already does the hardware thing (`wrf32d`). Here Xenia is wrong and the port is right.
- `fctiw`/`fctiwz` saturate correctly, but the upper word is sign-extended. On hardware that word is undefined, so
  only the low word should ever be compared.
- Trap instructions are ignored by default (`ignore_trap_instructions = true`).

**b. Stage 2 elevator softlock (before the Hod fight).** Reported three times, and still present in the June 2026
build. A script trigger never fires in Xenia; it works on a real console. If the port's elevator works and Xenia's
does not, that is **Xenia's fault**. The cause is unknown: it could be thread timing or the JIT. Whole-run checks
cannot go past that point on Xenia. Per-function recordings from before it are still valid.

**c. Pause-menu crashes (while saving, while changing equipment).** Saving goes through Xenia's emulated storage and
profile system (the kernel's content functions), not only game code, so differences on those paths are **Xenia's
fault**. Equipment-change crashes are unexplained. Recordings should avoid both until they are understood.

**d. Occlusion queries (`occlusion_query = "strict"`).** The game asks the GPU how many pixels a lens flare drew and
reads the answer back into memory. Xenia's answer depends on the mode and on timing (strict waits at most 2 ms). A
real console's counts also differ from a PC GPU's. The port's renderer gives its own numbers. So a difference in
those words, or in values computed from them, is **neither side's fault**: it is an **input**, like the timer. It
should be fed to the port from the recording, or listed in `divergence_ignore.txt`. It is not a judge question.

**e. (Found here, not in the entry.) Thread order and timing.** Runs differ in file order, cutscene timing and even
whether loading finishes. A frame-level difference caused by that is **nobody's fault**, and it will drown out real
differences unless checks are per function (§6) or the deterministic mode exists.

## 6. How DP's judge can settle who is right, per function

DP's judge checks each function's C against Ghidra's p-code on test inputs. Where p-code does not match the
architecture, it uses the documented fixups (the fma switch, `fctiw`, `stfs`, refused estimates). The judge can
referee a disagreement on one real call like this:

1. **Record the real call in XeniaRedux** (hooks R2–R3 below). Take the entry registers, every memory word the
   function read, and its exit registers and memory writes.
2. **Run the same call three ways:**
   - the port's C;
   - the judge's p-code model;
   - Xenia's JIT result, which the recording already has.
3. **Read the verdict:**

   | Result | Whose fault | What happens |
   |---|---|---|
   | port = judge ≠ Xenia | **Xenia** | Log it as a Xenia accuracy case, add it to a known-differences list, and maybe fix it in the fork. |
   | port ≠ judge | **The port** | Goes back through the pipeline agents, as usual (no hand edits). |
   | all three agree | Neither | The difference came from earlier state, thread order or GPU feedback: look one step earlier. |
   | judge's model is itself in doubt (fma, estimates) | Open | A decision for you, settled by the IBM manuals and hardware vectors, never by Xenia. |

**Hardware vectors.** 151 of Xenia's instruction test files are marked as generated (commit "Generated tests from
Rick", 2019). Their results match real-chip behaviour (the `fres` estimate above), so they look hardware-captured.
They cover NaNs, denormals, estimates and saturation.

**They do not settle the fma question.** They use only 12 edge values (0, ±1, infinities, NaNs, the smallest
denormal…). I checked all 1,024 finite `fmadd` cases and 1,024 `fmadds` cases. Fused and two-rounding results
**never differ** on those inputs, so these vectors cannot decide between the two models (`eval/fma_vectors.py`).

Whether DP may use these vectors as judge inputs is your decision. They are third-party data (BSD-3), and the rule
is that third-party material stays outside DP.

## 7. Recommendation and a rough plan for the fork

**Recommendation: adopt XeniaRedux as the Xbox 360 reference, in three phases.** Start with per-function recording,
which needs no repeatability. Treat whole-game frame lockstep as a later, separate project.

**Phase 1: per-function evidence (small–medium, no determinism needed).**
- **R1. No-window mode:** null presenter, `--gpu=null` allowed, no window.
- **R2. Call recorder:** for a list of guest addresses, log each call's thread, sequence number, entry registers,
  the memory words it read and wrote, and its exit registers. Keep it streamed and capped, following the memory rule
  from the lockstep work.
- **R3. One-function runner:** load the XEX, set given registers and memory, run `Processor::Execute`, and dump the
  result. This makes Xenia a second opinion beside the judge.
- **R4. Coverage export** to DP's `coverage.txt`.

**Phase 2: frame-level tools (medium).**
- **R5. Input replay driver** keyed by vblank, plus a recorder.
- **R6. RAM dumps** of changed blocks at vblank, in the `--ram-frames` format.
- **R7. Store watch** with PC, LR and value.
- **R8. Occlusion-query log**, so the port gets the same numbers.

These are useful for looking around even before runs repeat.

**Phase 3: deterministic mode (large).**
- **R9. Deterministic scheduling:** one scheduler thread runs guest threads in a fixed order, switching only at
  kernel calls or after fixed instruction counts. The game clock and vblanks are counted, not timed. File reads
  finish at fixed points. Sound is off and the GPU is null or synchronous.
- **Proof:** run the same input twice and compare a RAM hash at every vblank.
- **R10. Save states:** finish the existing skeleton.

Only after R9 can a Flycast-style lockstep run (R5 + R6 + R10).

The risk is real: Xenia's kernel leans on real threads and timeouts, and the change could move bugs around (the
elevator softlock could appear or vanish).

**One question for you, before Phase 3.** The Xbox 360 port will also run the game's ~40 threads. "Zero divergence"
for a multi-threaded game needs a decision on what counts as the same order of events, on both sides. That choice
shapes R9, so it is worth settling first.

## Reproduce

```
cd C:\Projects\XeniaRedux
python xenia-build.py setup
pwsh -File eval\guarded_build.ps1 -Jobs 8 -DeadlineMin 95
pwsh -File eval\boot_run.ps1 -Seconds 150 -Tag run1
```

`boot_run.ps1` writes its log and snapshots next to itself. `portable.txt` in the exe's folder keeps Xenia's config
and content there.

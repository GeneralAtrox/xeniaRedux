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

- (Update: the replayed input in §8 now reaches the title screen and play.)
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

## 8. The recorder hooks (branch `redux-recorder`)

Four of the small hooks from §4 and §7 are now built: R1, R4, R5 and the register half of R2. Together they make a
per-function recorder. Everything is still local: nothing was pushed, and the BSD licence is kept.

### What was added

| Hook | Switch | What it does |
|---|---|---|
| **Input replay** (R5) | `--replay_input="START@2600,A@5100+10"` | Presses buttons on controller 1 from a list, using DP's syntax: button `@` vblank, with an optional `+` length (default 10 vblanks). `@file` reads the list from a file. Buttons: `A B X Y START BACK LB RB LS RS GUIDE UP DOWN LEFT RIGHT`, the triggers `LT RT`, and full stick pushes `LUP LDOWN LLEFT LRIGHT RUP RDOWN RLEFT RRIGHT`. Every change is written to the log. |
| **No window** (R1) | `--no_window=true` | Runs with no window. A hidden screen is made only when snapshots are asked for. Dialogs answer themselves, as with `--headless`. |
| **Snapshots and timed exit** | `--snapshot_vblanks=8760,9300`, `--snapshot_dir=…`, `--exit_after_vblanks=9400` | Saves what the game shows as a PNG at those vblanks. At the exit vblank it waits for the snapshots, saves the logs and quits by itself, so nothing is lost to a forced stop. |
| **Call log** (R2, registers only) | `--call_log_functions=82338a88,…` (or `@file`), `--call_log_path=calllog.bin`, `--call_log_max_mb=450`, `--call_log_per_function_limit=20000` | For each listed function, writes one record when it starts and one when it returns, in the order they happened across all threads. Each record has the thread, a sequence number, the vblank, LR, r1, r3–r10 and f1–f13. At the return, r3 and f1 hold the results. The JIT adds this to the function's code, the same way as the existing `--trace_functions` counters. It writes to disk as it goes, stops at the size cap (450 MB by default, with a "truncated" marker), and keeps at most 20,000 calls per function. |
| **Trust marks** | (automatic, in the call log) | When Xenia compiles a listed function, it checks the instructions for the risks in §5a and stores them in the log: estimate instructions, fused multiply-add (and whether this PC fuses it), `stfs`, `fctiw` and traps. |
| **Coverage export** (R4) | `tools/redux/coverage_to_dp.py` | Turns `--trace_function_coverage` output into DP's `coverage.txt` (the start address of every block that ran). It reads DP's `decomp/image.bin` to find where blocks end. |

Two small readers sit in `tools/redux/`:

- `calllog_read.py summary|dump|pairs|trust LOG` lists the calls, prints records, writes matched start/return pairs
  as JSON lines, or writes the trust list.
- `coverage_to_dp.py TRACE.bin --image image.bin -o coverage.txt` does the coverage export.

`eval/redux_run.ps1` runs Too Human with all of this. Every run has a time limit, sound off, a RAM check every 3 s
(stopping at 95%), and the whole process tree stopped at the end.

**Trust levels.** DP's judge, not Xenia, decides any disagreement inside a function marked "not trusted".

| Level | When | Why |
|---|---|---|
| not-trusted | estimate instructions (`fres`, `frsqrte`, `vrefp`, `vrsqrtefp`, `vexptefp`, `vlogefp`) or fused multiply-add (`fmadd` family, `vmaddfp`, `vnmsubfp`) | Xenia gives exact values where the chip gives estimates; fused or not depends on this PC (§5a) |
| caution | `stfs`, `fctiw`/`fctiwz`, traps | Xenia rounds where the chip selects bits; compare only the low word; traps are ignored |
| trusted | none of these | |

### Too Human results

**The cutscenes are skipped, and the game reaches play, with no one at the controls.** The final run (`final2`) used
`START@2600,START@4500,A@5100,A@5700,A@6300,A@6900,B@8100,B@8400,B@8700,B@9000,LUP@9600+400`, with no window and
the call log on. It took 179 s, and RAM peaked at 87.6%. The snapshots are in `eval/redux_final2_snapshots.png`.

| Vblank | Press | What the snapshots and log show |
|---|---|---|
| 2600 | START | **Skips the opening cutscene.** The screen goes black, and the title screen is up by 4200. In a run with no input, the cutscene was still playing at 2400 and 3600. |
| 4500 | START | Main menu |
| 5100–6900 | A ×4 | Start Campaign, "proceed without saving", class choice (Champion), confirm |
| 8100 | B | **Skips the level's airship intro.** The game asks for the level file (`world01.zzz`) on the very next log line, and the loading screen is up by 8160. In a run without B, the intro was still playing at 9400. START or A does not skip it. B only works once the intro has finished loading, so the later B presses are spares; in play they did nothing visible. |
| 8460–9600 | | **Play.** Baldur stands in the level's first room, idle. |
| 9600–10000 | left stick up | **Baldur walks** out of the doorway into the courtyard (snapshots 9800, 10000, 10200), so the game is taking the replayed stick. |

**The call log was written and read back.** Seven functions were logged, chosen to cover each trust level:

| Function | Calls logged | Trust | Why |
|---|---|---|---|
| `82338a88` | 20,000 (cap) | trusted | plain integer code |
| `82807828` | 100 | trusted | ends with a tail call into a register-restore helper; the return is still logged |
| `82c45138` | 2,268 | trusted | two instructions (`lwz r3,0xe4(r3); blr`): a getter |
| `82817e08` | 20,000 (cap) | not trusted | 8 fused multiply-adds, 3 `fctiw` |
| `8281dd38` | 20,000 (cap) | not trusted | 2 fused multiply-adds, 3 `stfs` |
| `8296fea8` | 8,307 | not trusted | 2 estimate instructions, 6 fused multiply-adds |
| `829a6838` | 14,708 | not trusted | 6 estimates, 24 fused multiply-adds, 14 `stfs` |

- The log is 37 MB, 170,766 records from vblank 88 to 10,200, and was not cut short. This PC has FMA, so Xenia fused
  every multiply-add.
- **Every start has its matching return** (85,383 pairs). The stack pointer (r1), the return address (LR) and the
  thread are the same at both ends of every pair, as they must be.
- The getter's logged calls fit its code: its only input is r3 (a pointer), and only r3 changes at the return. The
  value it loads from memory cannot be checked yet, because memory is not logged (see Limits).
- The trust list is `eval/redux_final2_trust.txt`, one line per function. `calllog_read.py pairs` writes each call
  as a JSON line with an `"xenia"` field that carries the same level, so DP can tell which disagreements Xenia
  cannot settle.

**Coverage export.** A coverage run through the opening (`cov1`, 4,300 vblanks) gave 42,846 compiled functions and
1,087,918 instructions run. That made 182,088 block starts in DP's `coverage.txt` format. Spot checks of
instructions that never ran showed real branch skips, not gaps in the data.

### Bug fixed on the way

With `--trace_function_coverage` on, Too Human crashed at the start (a jump to address 0 at `82EC5470`). Upstream
Xenia's coverage counter (`lock inc`) changes the CPU's condition flags. The JIT had already compared, and it
branched on those changed flags. The counter now saves and restores the flags (`pushfq`/`popfq`). With that, the
coverage run went through the menus without a crash.

### Limits

- **Registers only.** The call log does not yet record vector registers, or the memory words a function reads and
  writes. §6's referee step needs those memory words. They are the next piece of R2.
- **x64 only.** The ARM64 JIT is not hooked.
- **Still not repeatable.** Vblanks come from a PC timer (41 to 57 per second on this machine, depending on load,
  not 60), so the same button list can land at slightly different moments in two runs. The menu timings held in
  every run that used them. The level intro, though, finished loading at different vblanks in different runs, which
  is why B is pressed several times. Whole runs still differ (§2).
- **Caps.** 20,000 calls per function, and 450 MB per log by default. Very busy functions fill their 20,000 in the
  first seconds, so the log shows the early calls. Pick the moment with `--exit_after_vblanks` and the per-function
  limit.
- **Controller 1 only.** The replay drives one controller, and it reads no real input while it runs.

## Reproduce

```
cd C:\Projects\XeniaRedux
python xenia-build.py setup
pwsh -File eval\guarded_build.ps1 -Jobs 8 -DeadlineMin 95
pwsh -File eval\boot_run.ps1 -Seconds 150 -Tag run1
```

`boot_run.ps1` writes its log and snapshots next to itself. `portable.txt` in the exe's folder keeps Xenia's config
and content there.

The recorder run from §8 (about 4 minutes):

```
pwsh -File eval\redux_run.ps1 -Seconds 500 -Tag final2 -NoWindow -ExitAfter 10200 `
  -Buttons "START@2600,START@4500,A@5100,A@5700,A@6300,A@6900,B@8100,B@8400,B@8700,B@9000,LUP@9600+400" `
  -Functions "8296fea8,829a6838,8281dd38,82817e08,82338a88,82807828,82c45138" `
  -Snapshots "2500,2800,4200,7700,8160,8460,8760,9060,9360,9600,9800,10000,10200"
python tools\redux\calllog_read.py summary eval\final2\calllog.bin
python tools\redux\calllog_read.py trust eval\final2\calllog.bin -o eval\final2\trust.txt
```

A coverage run adds `-Coverage`. Its output then goes to DP's format with
`python tools\redux\coverage_to_dp.py eval\<Tag>\trace.bin --image C:\Projects\TooHuman\decomp\image.bin -o coverage.txt`.

---
phase: quick-261122-kyo
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - test/test_console_list_scroll.cpp
  - test/test_remember_military_board_idx.cpp
  - CMakeLists.txt
  - src/core/core.cpp
  - include/jjffe/core/patch_gates.h
  - jjffe.cfg
  - docs/PATCH_INTEGRATION_PRIORITY.md
  - docs/ANDYJ_GATE_TABLE.md
  - docs/ANDYJ_PATCH_INTEGRATION_STATUS.md
autonomous: true
requirements: [ANDYJ-CHANGELOG-L732-735]

must_haves:
  truths:
    - "FUNC_000355 matches ffe.asm L27550-27570 in every state: with DATA_009050 = DATA_008692 = DATA_009051 = 0 it returns 1 for param 1 and -1 and leaves DATA_009050 unchanged, returns 0 for param 0; with count 5 / 8692 1 / 9050 0, FUNC_000355(2) == 0 and DATA_009050 == 2. The invented guard line `if (!DATA_009050 && !DATA_008692 && !DATA_009051) return 0;` is gone, in its own ungated fix(core) commit that lands first"
    - "With RememberMilitaryBoardIdx=0 (default), re-opening the military board list (FUNC_000515 'back', FUNC_000326 with DATA_009054 == 7) leaves DATA_009050 == 0 exactly as NASM does (FUNC_000342's reset), whatever the remembered index holds"
    - "With RememberMilitaryBoardIdx=1, after scrolling the list to row k (FUNC_000355 success), FUNC_000515 / FUNC_000326 reopen it at DATA_009050 == k; FUNC_000490, FUNC_000491 and FUNC_000320(param < 2) reset it to 0"
    - "The clamp is AJ's bytes (0x41b442..0x41b472), not '0..count': idx > count -> count (static := count); idx < 0 -> 1 (static := 1); otherwise idx as-is (idx == count kept); count is DATA_009051 before the 0x983f trailer row; an empty list writes static := 0 ungated and shows title 0x990b"
    - "The ten AJ writes to the port-only static s_aj899f6d (FUNC_000320/323/324/355/490/491/493 empty-list/998 + the two clamp stores) are unconditional; the only gated statement is the restore block in FUNC_000493. g_patchGates.RememberMilitaryBoardIdx is read exactly once in core.cpp. No NASM-visible memory changes with the flag off (research A2)"
    - "PatchGates is unchanged: 50 bools, 51 `COUNT_IF(` lines; only the patch_gates.h doc comment for RememberMilitaryBoardIdx changes. jjffe.cfg stays all-CRLF (113 lines) with one new comment line above RememberMilitaryBoardIdx=0"
    - "Full ctest: 204 baseline -> 205 after fix(core) (ConsoleListScrollTests) -> 206 after feat(patches) (RememberMilitaryBoardIdxTests), 0 failures; AsmCallOracle* and LogicReplay* green with no golden regenerated"
    - "Three commits on top of 66f82971, in order fix(core), feat(patches), docs(patches), each staged by explicit path and ending in `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; docs/ANDYJ_PATCHES_ANALYSIS.md stays untracked"
  artifacts:
    - path: "test/test_console_list_scroll.cpp"
      provides: "ConsoleListScrollTests: FUNC_000355 ASM return values incl. empty list; DATA_008692 set via FindLabel"
      min_lines: 60
    - path: "test/test_remember_military_board_idx.cpp"
      provides: "RememberMilitaryBoardIdxTests: flag off/on reuse, entry resets, clamp, gate at call time; stubs only DATA_007639"
      min_lines: 200
    - path: "src/core/core.cpp"
      provides: "s_aj899f6d static + AJ_899f6d oracle label + 8 ungated write sites + gated clamp/restore in FUNC_000493"
      contains: "g_patchGates.RememberMilitaryBoardIdx"
    - path: "CMakeLists.txt"
      provides: "two test blocks after FixEscapePodThrustersTests"
      contains: "RememberMilitaryBoardIdxTests"
  key_links:
    - from: "src/core/core.cpp FUNC_000355"
      to: "s_aj899f6d"
      via: "DATA_009050 = param_1; s_aj899f6d = param_1; on the success path only"
      pattern: "s_aj899f6d = param_1;"
    - from: "src/core/core.cpp FUNC_000493"
      to: "DATA_009050"
      via: "if (g_patchGates.RememberMilitaryBoardIdx) { clamp; DATA_009050 = idx; } before param_1[0] = 0x990a"
      pattern: "if \\(g_patchGates\\.RememberMilitaryBoardIdx\\)"
    - from: "src/core/core.cpp OracleLabelStorageTable"
      to: "s_aj899f6d"
      via: "put(\"AJ_899f6d\", &s_aj899f6d, sizeof(s_aj899f6d))"
      pattern: "put\\(\"AJ_899f6d\""
    - from: "test/test_remember_military_board_idx.cpp"
      to: "FUNC_000515 / FUNC_000326 / FUNC_000490 / FUNC_000491 / FUNC_000355"
      via: "real calls in MAP_32BIT memory with DATA_009004 entries in g_bss8804+0x3cba"
      pattern: "FUNC_000515\\("
---

<objective>
Port AndyJ's RememberMilitaryBoardIdx (cfg case 6, AJ changelog L732-735) behind the EXISTING default-OFF flag
`g_patchGates.RememberMilitaryBoardIdx`, after first fixing a pre-existing port bug in FUNC_000355 that sits on the same
path. With the flag off, behaviour is byte-identical to NASM.

Primary source: `.planning/quick/261122-kyo-andyj-remembermilitaryboardidx/261122-kyo-RESEARCH.md` (committed in
66f82971). Spot-check line numbers; do not re-research.

Locked decisions (orchestrator):
- D1: first commit, ungated `fix(core)`: delete the invented guard in FUNC_000355, regression test
  `test/test_console_list_scroll.cpp` (ctest `ConsoleListScrollTests`), RED first (rc 134 + `Assertion`, never 139).
  DATA_008692 is set via FindLabel (it is a per-TU `static uint32_t` in data_globals.h L891).
- D2: `static int32_t s_aj899f6d = 0;` right after DATA_009050 (core.cpp L2358) + OracleLabelStorageTable
  `put("AJ_899f6d", ...)`. The ten AJ writes are UNCONDITIONAL (research A2); only the FUNC_000493 read/clamp is gated.
  Clamp exactly per bytes: idx > count -> count; idx < 0 -> 1; else idx; DATA_009050 = idx. Comment cites AJ 0x41b442 /
  gate 0x7a5cfe / cfg 0x7a39d5 case 6 / changelog L732-735.
- D3: the ExtendedLifetime 0x403779 wrapper at AJ 0x41b404 is OUT OF SCOPE (key ExtendedLifetime_HC_Military_Missions,
  [PATCHES_FFE]); docs record it as a follow-up only.
- D4: feature test `test/test_remember_military_board_idx.cpp` (ctest `RememberMilitaryBoardIdxTests`) per RESEARCH
  section 6; label-free checks first so RED fails on behaviour (rc 134). Only stub DATA_007639 (writes NUL). No blob, no
  VideoInit. Both tests registered after FixEscapePodThrustersTests; ok lines tagged "flag off" / "flag on".
- D5: docs commit: PATCH_INTEGRATION_PRIORITY.md (3E clamp wording, writer list, ExtendedLifetime follow-up, 3E
  complete, Week-3 item, tracker row), ANDYJ_GATE_TABLE.md row L68, ANDYJ_PATCH_INTEGRATION_STATUS.md.

DO NOT:
- change any PatchGates field, readFlag or COUNT_IF (stay 50 bools / 51 `COUNT_IF(`). The flag already exists
  (patch_gates.h:74, patch_gates.cpp:114/175, jjffe.cfg:58, test_patch_gates.cpp:30).
- port the ExtendedLifetime wrapper around the two FUNC_000034 calls in FUNC_000493 (D3).
- gate the ten s_aj899f6d writes (D2 / A2).
- edit generated files (data_globals.h, data_batch3_init.h, logic_label_table.inc, asm_label_table.inc,
  services_tables.inc, ffe.asm, ffedat.asm, data_blob/), or regenerate any golden. Any AsmCallOracle* / LogicReplay*
  failure: STOP, report, do not commit.
- stage docs/ANDYJ_PATCHES_ANALYSIS.md (untracked, not ours) or anything under .planning/. Stage by explicit path only.
- use grep for patterns containing `$` or CR (the grep here is a ugrep wrapper); use python. Do not pipe a test binary
  when its rc is being checked.

Output: three commits (fix(core), feat(patches), docs(patches)) + `261122-kyo-SUMMARY.md` (written, NOT committed).
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@AGENTS.md
@.planning/quick/261122-kyo-andyj-remembermilitaryboardidx/261122-kyo-RESEARCH.md
@test/test_escape_pod_thrusters.cpp
@test/test_photo_bbs_reply.cpp

SP = `/tmp/claude-1000/-home-alex-Src-Reverse-games-jjffe/da890218-d44e-4cec-887f-b610155f4ef3/scratchpad`.
Logs: `SP/baseline_kyo.log`, `SP/red_kyo_fix.log`, `SP/kyo_fix_ctest.log`, `SP/red_kyo.log`, `SP/kyo_feat_ctest.log`.
Full ctest takes ~30 min: run it with `run_in_background` (or a timeout that covers it) and wait for completion; never
pipe ctest's rc away. Build dir is Ninja (`build/build.ninja`).

Verified at planning time (core.cpp line numbers; re-locate by text):
- L2353 `static i32_unaligned& DATA_009051 = ...(g_bss8804 + 0x48da);` L2357 `DATA_009063` (+0x4921, int8),
  L2358 `static i32_unaligned& DATA_009050 = *reinterpret_cast<i32_unaligned*>(g_bss8804 + 0x48d6);  // Console index offset`
  -> insert the static directly after L2358.
- L15793 FUNC_000998: `    DATA_009091 = 0;` at L15800 -> insert `    s_aj899f6d = 0;  // AJ 0x436acf` after it.
- L41705 FUNC_000320: inside `if (param_1 < 2) {`, after `        DATA_009067 = 0;` (L41710) -> `        s_aj899f6d = 0;  // AJ 0x416833`.
- L41744 FUNC_000323: after `    DATA_009067 = 0;` (L41748), before `FUNC_000359();` -> `    s_aj899f6d = 0;  // AJ 0x4168e5`.
- L41774 FUNC_000324: inside `if (DATA_008870 == '*' || DATA_008870 == '0') {`, after `        DATA_009067 = 0;` (L41779)
  -> `        s_aj899f6d = 0;  // AJ 0x4169ee`.
- L42280 FUNC_000355 (current):
  ```cpp
  uint32_t FUNC_000355(int param_1) {
      if (!DATA_009050 && !DATA_008692 && !DATA_009051) return 0;   // <- D1: delete (MiMo artefact, 23b9c278)
      param_1 = param_1 + DATA_009050;
      if ((-1 < param_1) && ((int32_t)DATA_008692 + param_1 <= DATA_009051)) {
          DATA_009050 = param_1;                                      // <- D2: add `s_aj899f6d = param_1;  // AJ 0x417b6a` after
          return 0;
      }
      return 1;
  }
  ```
- L44123 FUNC_000490: after `    DATA_009063 = 2;` (L44126 = ffe.asm L33306 byte store to +0x4921) ->
  `    s_aj899f6d = 0;  // AJ 0x41b311`.
- L44133 FUNC_000491: first statement `    s_aj899f6d = 0;  // AJ 0x41b344`.
- L44538 FUNC_000493, current tail: `    FUNC_000034(0, 3);` / `    FUNC_000034(0, 0);` /
  `    param_1[0] = DATA_009051 == 0 ? 0x990b : 0x990a;` (L44555, THE site, ffe.asm L33407-33412) /
  `FUNC_000352(...)` / `FUNC_000348(0x983f, ...)` / `FUNC_000494();`.
- L44562 FUNC_000515(int32_t*) = `FUNC_000493(param_1);`. L52164 FUNC_000326: `case 7:` calls FUNC_000493 on a local
  0x30-byte buffer. DATA_009054 = `uint32_t&` at g_bss8804+0x48e8 (data_globals.h:1251).
- L57558 `    put("DATA_007746", &DATA_007746, sizeof(DATA_007746));  // quick 261121-voh: ...` -> insert the AJ_899f6d put
  directly after it. Table has 178 of 256 entries.
- DATA_009004 = inline `NewsEntriesHead` (529 bytes) at g_bss8804+0x3cba: 0x18-byte entries (6 int32), terminated by a
  zero first dword. FUNC_000493 copies entry[0,4,2,1,5,3] -> ctx[0,1,2,3,9,0xb] then FUNC_000345.
- DATA_008862 = `int32_t&` at g_bss8804+0xcc (fake ship pointer, read at +0xa0 by 490/491; FUNC_000493 stores it in ctx[7]).
- DATA_008692: `static uint32_t` (data_globals.h:891), label `DATA_008692` in LogicLabelStorageTable
  (logic_label_table.inc:228, 4 bytes) -> write exactly 4 bytes through `FindLabel` (never the test TU's own copy, never
  an 8-byte intptr_t store).
- DATA_007639: `static intptr_t` (core.cpp L2336), label in logic_label_table.inc:175; stub precedent
  test_photo_bbs_reply.cpp:76 `int32_t StubExpand(intptr_t buf, uint32_t fmt, intptr_t args)` (write NUL to buf,
  record fmt; do NOT memcpy args).
- CMakeLists.txt L158-160 = the FixEscapePodThrustersTests block. Baseline `ctest -N`: 204.
- jjffe.cfg: 112 lines, 112 CRLF; line 58 `RememberMilitaryBoardIdx=0`, line 56 is a precedent comment line.
- patch_gates.h L72-73 doc comment, L74 `    bool RememberMilitaryBoardIdx = false;` (unchanged).

<interfaces>
Test-side declarations (all external-linkage definitions in core.cpp):
```cpp
namespace jjffe {
uint32_t FUNC_000355(int);
void FUNC_000490(intptr_t);
void FUNC_000491(intptr_t);
void FUNC_000493(int32_t*);
void FUNC_000515(int32_t*);
void FUNC_000326(void);
void FUNC_000320(uint8_t);
}
```
Harness pieces to copy verbatim from test/test_escape_pod_thrusters.cpp: `FindLabel` (searches Logic, Asm, Oracle label
tables by name, skips `@blob`), `Slot`, `SlotGuard` (intptr_t-sized; use ONLY for DATA_007639). g_bss8804:
`uint8_t[0x556c]` reference via "jjffe/data_globals.h" (shared across TUs). "jjffe/asm_label_table.h" for the tables.
</interfaces>
</context>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: baseline, ConsoleListScrollTests RED, delete the FUNC_000355 guard, full ctest 205, fix(core) commit</name>
  <files>test/test_console_list_scroll.cpp, CMakeLists.txt, src/core/core.cpp</files>
  <behavior>
    All via memcpy Get/Put on g_bss8804 + 0x48d6 (DATA_009050) / 0x48da (DATA_009051), and a 4-byte write through
    `FindLabel("DATA_008692")` (assert non-null). Snapshot g_bss8804 and the 4 DATA_008692 bytes at start, restore at end.
    - Empty list (9050 = 8692 = 9051 = 0): `FUNC_000355(1) == 1` (FIRST assert; the RED abort), 9050 still 0;
      `FUNC_000355(-1) == 1`, 9050 still 0; `FUNC_000355(0) == 0`, 9050 == 0.
    - count 5, 8692 1, 9050 0: `FUNC_000355(2) == 0`, 9050 == 2; `FUNC_000355(3) == 1` (2+3+1 = 6 > 5), 9050 still 2;
      `FUNC_000355(-3) == 1` (-1 < 0), 9050 still 2; `FUNC_000355(2) == 0` (1 + 4 == 5 kept, boundary), 9050 == 4;
      `FUNC_000355(-4) == 0`, 9050 == 0.
    - Print got/want before each assert; ok lines `ok: empty list ...`, `ok: bounded scroll ...`; end with
      `ConsoleListScroll: PASS`, return 0.
  </behavior>
  <action>
    0. BASELINE (nothing touched yet), from the repo root:
       `cd build && ninja -j4 && { ctest --output-on-failure > SP/baseline_kyo.log 2>&1; echo "rc=$?" >> SP/baseline_kyo.log; }`
       Expect `100% tests passed, 0 tests failed out of 204`, rc=0. A pre-existing failure that is not AsmCallOracle* /
       LogicReplay* is noted in SUMMARY and does not block; an AsmCallOracle*/LogicReplay* failure = STOP.

    1. Create test/test_console_list_scroll.cpp in the style of test_escape_pod_thrusters.cpp (`#undef NDEBUG`,
       <cassert>, `std::setvbuf(stdout, nullptr, _IONBF, 0)`). Header comment: quick 261122-kyo, ungated regression test
       for FUNC_000355 vs ffe.asm L27550-27570 (eax = p + [9050]; jl -> 1; edx = [8692] + eax; cmp edx,[9051]; jg -> 1;
       else store, 0); the deleted guard came from commit 23b9c278 (MiMo pipeline); effect of the bug: FUNC_000356
       dispatched a console redraw on a scroll key in an empty list; why DATA_008692 goes through FindLabel (header
       static, one copy per TU). Cases per <behavior>. No blob, no VideoInit, no mmap needed.

    2. CMakeLists.txt, directly after the `add_test(NAME FixEscapePodThrustersTests ...)` line add exactly 3 lines:
       `add_executable(test_console_list_scroll test/test_console_list_scroll.cpp)`
       `target_link_libraries(test_console_list_scroll "LINKER:--start-group" jjffe_game jjffe_platform jjffe_core jjffe_data "LINKER:--end-group")`
       `add_test(NAME ConsoleListScrollTests COMMAND test_console_list_scroll WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})`

    3. RED (core.cpp untouched), from the repo root, no pipe on the binary:
       `cd build && ninja test_console_list_scroll && cd .. && { build/test_console_list_scroll > SP/red_kyo_fix.log 2>&1; echo "rc=$?" >> SP/red_kyo_fix.log; }`
       Required: rc=134 and an `Assertion` line naming `FUNC_000355(1)`. rc=139 = fixture bug: fix the test, re-run.

    4. FIX (D1): in core.cpp FUNC_000355 delete exactly the line
       `    if (!DATA_009050 && !DATA_008692 && !DATA_009051) return 0;` — no other core.cpp change in this commit.
       `cd build && ninja -j4 && cd .. && build/test_console_list_scroll` -> `ConsoleListScroll: PASS`, rc 0.

    5. Full suite: `cd build && { ctest --output-on-failure > SP/kyo_fix_ctest.log 2>&1; echo "rc=$?" >> SP/kyo_fix_ctest.log; }`
       Expect `100% tests passed, 0 tests failed out of 205`, rc=0. AsmCallOracle*/LogicReplay* failure = STOP, no commit.

    6. Commit: `git add src/core/core.cpp test/test_console_list_scroll.cpp CMakeLists.txt`; `git diff --cached
       --name-only` must list exactly those 3. Message (HEREDOC):
       ```
       fix(core): drop invented empty-list guard in FUNC_000355

       ffe.asm L27550-27570 has no early-out: with DATA_009050, DATA_008692
       and DATA_009051 all zero, a scroll of +1 or -1 fails the bounds check
       and returns 1. The C++ guard (MiMo pipeline, 23b9c278) returned 0,
       so FUNC_000356 dispatched a console redraw on a scroll key in an
       empty list where the original does nothing. Adds
       ConsoleListScrollTests. Ungated: restores NASM behaviour. Precedes
       the AndyJ RememberMilitaryBoardIdx port, which writes in this
       function.

       Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
       ```
  </action>
  <verify>
    <automated>cd /home/alex/Src/Reverse/games/jjffe && python3 - <<'EOF'
import subprocess, re
SP = "/tmp/claude-1000/-home-alex-Src-Reverse-games-jjffe/da890218-d44e-4cec-887f-b610155f4ef3/scratchpad"
def rd(n): return open(f"{SP}/{n}", errors="replace").read()
def sh(*a): return subprocess.run(a, capture_output=True, text=True, check=True).stdout
b = rd("baseline_kyo.log"); assert "\nrc=0\n" in b + "\n" and "0 tests failed out of 204" in b, "baseline"
r = rd("red_kyo_fix.log"); assert re.search(r"^rc=134$", r, re.M), "red rc"
assert any("Assertion" in l and "FUNC_000355(1)" in l for l in r.splitlines()), "red assertion"
f = rd("kyo_fix_ctest.log"); assert re.search(r"^rc=0$", f, re.M) and "100% tests passed, 0 tests failed out of 205" in f, "fix ctest"
assert sh("git", "log", "-1", "--format=%s").startswith("fix(core)")
assert "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" in sh("git", "log", "-1", "--format=%B")
assert sh("git", "rev-parse", "HEAD~1").startswith("66f82971"), "parent"
assert sorted(sh("git", "show", "--name-only", "--format=", "HEAD").split()) == ["CMakeLists.txt", "src/core/core.cpp", "test/test_console_list_scroll.cpp"]
d = sh("git", "show", "-U0", "--format=", "HEAD", "--", "src/core/core.cpp").splitlines()
rem = [l for l in d if l.startswith("-") and not l.startswith("---")]
add = [l for l in d if l.startswith("+") and not l.startswith("+++")]
assert rem == ["-    if (!DATA_009050 && !DATA_008692 && !DATA_009051) return 0;"] and add == [], (rem, add)
c = sh("git", "show", "-U0", "--format=", "HEAD", "--", "CMakeLists.txt").splitlines()
assert len([l for l in c if l.startswith("+") and not l.startswith("+++")]) == 3 and not [l for l in c if l.startswith("-") and not l.startswith("---")]
cm = open("CMakeLists.txt").read(); assert cm.index("FixEscapePodThrustersTests") < cm.index("ConsoleListScrollTests")
print("task1 OK")
EOF</automated>
  </verify>
  <done>
    Baseline 204/204. RED rc 134 on `FUNC_000355(1) == 1`. One fix(core) commit on 66f82971 with 3 files; core.cpp diff is
    exactly the deleted guard. Full ctest 205/205.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: RememberMilitaryBoardIdxTests RED, then the static + label + ungated writes + gated clamp (GREEN)</name>
  <files>test/test_remember_military_board_idx.cpp, CMakeLists.txt, src/core/core.cpp</files>
  <behavior>
    Memory: one `MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT` mmap of 0x1000 (assert < 0x80000000): ctx (12 int32, zeroed
    before each call) at +0x0, fake ship (0x200 bytes, zeroed, u32 +0xa0 = 0x12345678) at +0x100. Put DATA_008862
    (i32 g_bss8804+0xcc) = fake ship address. Snapshot all of g_bss8804 at start; restore it, the DATA_008692 bytes and
    `g_patchGates = PatchGates{}` at end. `SlotGuard s7639("DATA_007639", &StubExpand)` for the whole run; StubExpand
    writes `*(char*)buf = 0`, records every fmt (ring of 16) and counts calls. Helpers: memcpy Get/Put i32 at
    0x48d6 (9050), 0x48da (9051), 0x3cba (9004 base), 0xcc (8862), u32 at 0x48e8 (9054); `SetEntries(n)` writes n
    distinct 0x18-byte entries (first dword non-zero, e.g. 0x9827 + i) then a zero dword, n <= 21; `Set8692(v)` 4-byte
    write via FindLabel("DATA_008692"). `AjSlot()` = `reinterpret_cast<int32_t*>(FindLabel("AJ_899f6d"))`, called
    LAZILY only from group 4 onwards (assert non-null there) so the RED run never prints `missing label`; read/write it
    as int32 (4 bytes) only.
    Title check: the first fmt recorded during a FUNC_000493 run is the title (FUNC_000352): 0x990a non-empty,
    0x990b empty. If later rows also call the expander, assert on that first recorded fmt only.

    Order (label-free groups 1-3 first; group 2's first assert is the RED abort):
    1. `ok: 1 flag off ...`: flag off, SetEntries(5), FUNC_000490(ctx) -> count == 6, 9050 == 0, title 0x990a; Set8692(1);
       `FUNC_000355(2) == 0` -> 9050 == 2; FUNC_000515(ctx) -> 9050 == 0 (vanilla reset), count == 6, title 0x990a;
       scroll to 2 again, Put 9054 = 7, FUNC_000326() -> 9050 == 0.
    2. `ok: 2 flag on ...`: same steps with the flag on -> after FUNC_000515, 9050 == 2 (FIRST flag-on assert = RED);
       scroll to 2, FUNC_000326() with 9054 = 7 -> 9050 == 2.
    3. `ok: 3 flag on ...` entry resets: scroll to 2, FUNC_000490(ctx) -> 9050 == 0, then FUNC_000515 -> 0; same with
       FUNC_000491(ctx); scroll to 2, `FUNC_000320(0)`, SetEntries(5) again, FUNC_000515 -> 0. (FUNC_000320(0) also sets
       core-static DATA_005315 = 1; run this sub-check last in group 3; if it perturbs later groups, move it to the end
       of the run and note it in SUMMARY.) Never call FUNC_000320(2), FUNC_000323, FUNC_000324 or FUNC_000998.
    4. `ok: 4 flag on clamp ...` (AjSlot): SetEntries(3), flag on, FUNC_000515 with static = 2 -> 9050 2, static 2;
       3 (== count) -> 3, static 3; 7 -> 3, static 3; -4 -> 1, static 1; SetEntries(0), static 5 -> 9050 0, static 0,
       title 0x990b.
    5. `ok: 5 flag off ...`: SetEntries(3), static 7 -> 9050 0, static still 7; SetEntries(0), static 7 -> static 0
       (ungated empty-list write), title 0x990b.
    6. `ok: 6 flag off ...`: SetEntries(5), FUNC_000490(ctx) (zeroes the static), then static := 2, Set8692(1), a failing
       `FUNC_000355(9) == 1` -> static still 2; a succeeding `FUNC_000355(1) == 0` -> static == 9050 == 1 (write is
       ungated, so this group runs with the flag off).
    7. `ok: 7 flag off/on gate read at call time`: static 2, SetEntries(5): flag on -> FUNC_000515 -> 2; then
       `g_patchGates = PatchGates{}` -> FUNC_000515 -> 0; flag on again (static re-set to 2) -> 2.
    Ok lines containing the flag-off results say "flag off" and never "flag on"; group 7's line says both. Print
    got/want (9050, 9051, static, title fmt) before every assert. Final line `RememberMilitaryBoardIdx: PASS`, rc 0.
  </behavior>
  <action>
    1. Create test/test_remember_military_board_idx.cpp (style of test_escape_pod_thrusters.cpp; includes <cassert>
       <cstdint> <cstdio> <cstring> <initializer_list> <sys/mman.h>, "jjffe/core/patch_gates.h",
       "jjffe/data_globals.h", "jjffe/asm_label_table.h", + the <interfaces> declarations). Header comment: quick
       261122-kyo, AJ changelog L732-735, cfg case 6 (cfg byte 0x7a39d5, gate 0x7a5cfe, sole reader 0x41b442 in AJ fn
       0x41b3b2 = FUNC_000493), default OFF; the AJ-new bss dword 0x899f6d -> port static s_aj899f6d (label AJ_899f6d);
       writes ungated, restore gated; the byte-exact clamp (>count -> count, <0 -> 1, ==count kept); why only
       DATA_007639 is stubbed (RESEARCH section 4 chain; the real expander strnlen's an uninitialised buffer); why
       FUNC_000323/324/998 are checked structurally (sound, BST, spawn paths); why groups 1-3 come before any label
       lookup (RED fails on behaviour). Cases per <behavior>.

    2. CMakeLists.txt, directly after the `add_test(NAME ConsoleListScrollTests ...)` line (Task 1) add exactly 3 lines:
       `add_executable(test_remember_military_board_idx test/test_remember_military_board_idx.cpp)`
       `target_link_libraries(test_remember_military_board_idx "LINKER:--start-group" jjffe_game jjffe_platform jjffe_core jjffe_data "LINKER:--end-group")`
       `add_test(NAME RememberMilitaryBoardIdxTests COMMAND test_remember_military_board_idx WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})`

    3. RED (core.cpp as committed in Task 1, no static/label/gate yet), from the repo root:
       `cd build && ninja test_remember_military_board_idx && cd .. && { build/test_remember_military_board_idx > SP/red_kyo.log 2>&1; echo "rc=$?" >> SP/red_kyo.log; }`
       Required: rc=134, an `Assertion` line, >= 1 `ok: 1 flag off` line before it, no `ok:` line containing
       "flag on", no `missing label`. A group-1 failure or rc 139 means the FIXTURE is wrong (re-check RESEARCH section 4):
       fix the test, never core.cpp.

    4. GREEN, core.cpp (D2), exactly these edits:
       a. After L2358 (DATA_009050):
          `static int32_t s_aj899f6d = 0;  // AJ 0x899f6d, RememberMilitaryBoardIdx (bss dword, not saved; quick 261122-kyo)`
       b. Ungated writes at the 7 sites listed in Verified facts (FUNC_000998, 320, 323, 324, 355, 490, 491), each with
          its `// AJ 0x......` comment as given there. FUNC_000355 gets `s_aj899f6d = param_1;` directly after
          `DATA_009050 = param_1;` (success path only).
       c. FUNC_000493: replace the single line `    param_1[0] = DATA_009051 == 0 ? 0x990b : 0x990a;` with:
          ```cpp
              // AndyJ RememberMilitaryBoardIdx (AJ changelog L732-735; cfg 0x7a39d5 case 6,
              // gate 0x7a5cfe, sole reader AJ 0x41b442 in fn 0x41b3b2). FUNC_000342 above zeroed
              // DATA_009050 (the NASM reset); with the flag on, restore the index last stored by
              // FUNC_000355 into AJ's bss dword 0x899f6d (s_aj899f6d). Clamp exactly as AJ's bytes
              // (0x41b450..0x41b472): idx > count -> count; idx < 0 -> 1; else idx as-is
              // (idx == count kept). count = DATA_009051 before the 0x983f trailer row.
              // The s_aj899f6d writes (here and in FUNC_000320/323/324/355/490/491/998) are AJ's
              // ungated stores; nothing but this gated block reads the static.
              if (DATA_009051 == 0) {
                  param_1[0] = 0x990b;
                  s_aj899f6d = 0;  // AJ 0x41b43b, ungated
              } else {
                  if (g_patchGates.RememberMilitaryBoardIdx) {
                      int32_t idx = s_aj899f6d;
                      if (idx > (int32_t)DATA_009051) { idx = (int32_t)DATA_009051; s_aj899f6d = idx; }
                      else if (idx < 0) { idx = 1; s_aj899f6d = idx; }
                      DATA_009050 = idx;
                  }
                  param_1[0] = 0x990a;
              }
          ```
          (This is the ONLY removed line in the feature diff.) Leave the FUNC_000034 calls unwrapped (D3).
       d. OracleLabelStorageTable, directly after the `put("DATA_007746", ...)` line:
          `    put("AJ_899f6d", &s_aj899f6d, sizeof(s_aj899f6d));  // quick 261122-kyo: test-visible RememberMilitaryBoardIdx index`
       Then `cd build && ninja -j4 && cd .. && build/test_remember_military_board_idx` -> PASS, and
       `(cd build && ctest -R "RememberMilitaryBoardIdx|ConsoleListScroll|PatchGates|FixEscapePodThrusters|PhotographBBS" --output-on-failure)` passes.
       No commit in this task.
  </action>
  <verify>
    <automated>cd /home/alex/Src/Reverse/games/jjffe && (cd build && ninja -j4 >/dev/null) && build/test_remember_military_board_idx > /tmp/claude-1000/-home-alex-Src-Reverse-games-jjffe/da890218-d44e-4cec-887f-b610155f4ef3/scratchpad/green_kyo.log 2>&1 && (cd build && ctest -R "RememberMilitaryBoardIdx|ConsoleListScroll|PatchGates" --output-on-failure) && python3 - <<'EOF'
import subprocess, re
SP = "/tmp/claude-1000/-home-alex-Src-Reverse-games-jjffe/da890218-d44e-4cec-887f-b610155f4ef3/scratchpad"
def sh(*a): return subprocess.run(a, capture_output=True, text=True, check=True).stdout
r = open(f"{SP}/red_kyo.log", errors="replace").read(); L = r.splitlines()
assert re.search(r"^rc=134$", r, re.M) and "missing label" not in r
ai = next(i for i, l in enumerate(L) if "Assertion" in l)
assert any(l.startswith("ok: 1 flag off") for l in L[:ai]) and not [l for l in L if l.startswith("ok:") and "flag on" in l]
assert open(f"{SP}/green_kyo.log").read().rstrip().endswith("RememberMilitaryBoardIdx: PASS")
d = sh("git", "diff", "-U0", "--", "src/core/core.cpp").splitlines()
rem = [l for l in d if l.startswith("-") and not l.startswith("---")]
add = "\n".join(l for l in d if l.startswith("+") and not l.startswith("+++"))
assert rem == ["-    param_1[0] = DATA_009051 == 0 ? 0x990b : 0x990a;"], rem
for s in ("0x41b442", "0x7a5cfe", "0x7a39d5", "case 6", "L732-735", 'put("AJ_899f6d", &s_aj899f6d, sizeof(s_aj899f6d));', "static int32_t s_aj899f6d = 0;"): assert s in add, s
src = open("src/core/core.cpp").read()
assert "if (!DATA_009050 && !DATA_008692 && !DATA_009051) return 0;" not in src
assert src.count("g_patchGates.RememberMilitaryBoardIdx") == 1
assert src.count("s_aj899f6d = 0;") == 8 and src.count("s_aj899f6d = param_1;") == 1 and src.count("s_aj899f6d = idx;") == 2 and src.count("= s_aj899f6d;") == 1
def body(sig):
    m = re.search(r"^" + re.escape(sig) + r".*?^\}", src, re.M | re.S); assert m, sig; return m.group(0)
def after(sig, anchor):
    ls = [l.strip() for l in body(sig).splitlines()]; i = ls.index(anchor); assert ls[i + 1].startswith("s_aj899f6d = "), (sig, ls[i + 1])
after("void FUNC_000320(uint8_t param_1) {", "DATA_009067 = 0;")
after("void FUNC_000323(void) {", "DATA_009067 = 0;")
after("void FUNC_000324(void) {", "DATA_009067 = 0;")
after("void FUNC_000998(void) {", "DATA_009091 = 0;")
after("uint32_t FUNC_000355(int param_1) {", "DATA_009050 = param_1;")
after("void FUNC_000490(intptr_t param_1) {", "DATA_009063 = 2;")
assert body("void FUNC_000491(intptr_t param_1) {").splitlines()[1].strip().startswith("s_aj899f6d = 0;")
b493 = body("void FUNC_000493(int32_t* param_1) {")
assert b493.index("FUNC_000034(0, 0);") < b493.index("g_patchGates.RememberMilitaryBoardIdx") < b493.index("FUNC_000352(")
assert "0x403779" not in b493 and "ExtendedLifetime" not in b493
assert src.index("i32_unaligned& DATA_009050") < src.index("static int32_t s_aj899f6d = 0;") < src.index("void FUNC_000998(void) {")
cm = open("CMakeLists.txt").read(); assert cm.index("ConsoleListScrollTests") < cm.index("RememberMilitaryBoardIdxTests")
print("task2 OK")
EOF</automated>
  </verify>
  <done>
    RED rc 134 on the first group-2 (flag on) assert with only group-1 "flag off" ok lines and no missing label. GREEN:
    the one removed core.cpp line is the FUNC_000493 ternary; the static, the label, 8 ungated write sites and the single
    gated restore block are in; RememberMilitaryBoardIdxTests, ConsoleListScrollTests and PatchGatesTests pass.
    Nothing committed yet.
  </done>
</task>

<task type="auto">
  <name>Task 3: header/cfg comments, full ctest 206, feat(patches) commit, docs, docs(patches) commit</name>
  <files>include/jjffe/core/patch_gates.h, jjffe.cfg, docs/PATCH_INTEGRATION_PRIORITY.md, docs/ANDYJ_GATE_TABLE.md, docs/ANDYJ_PATCH_INTEGRATION_STATUS.md</files>
  <action>
    1. include/jjffe/core/patch_gates.h: replace ONLY the two doc-comment lines above
       `    bool RememberMilitaryBoardIdx = false;` (L72-73) with one line:
       `    /// RememberMilitaryBoardIdx - FUNC_000493 (AJ 0x41b442, gate 0x7a5cfe, case 6): reopen the military board list at the index last stored by FUNC_000355 (AJ dword 0x899f6d -> s_aj899f6d), clamped as AJ's bytes: > count -> count, < 0 -> 1, else as-is. AJ changelog L732-735. Test RememberMilitaryBoardIdxTests.`
       The bool line is unchanged; still 50 `bool` fields.

    2. jjffe.cfg (CRLF), byte-safe python: read bytes, assert exactly one b"\r\nRememberMilitaryBoardIdx=0\r\n", replace
       with b"\r\n# RememberMilitaryBoardIdx returns the military mission list to its last scroll position, clamped to the list length.\r\nRememberMilitaryBoardIdx=0\r\n",
       write bytes. Result 113 lines, 113 CRLF; the comment has no '=' and no '['.

    3. Full suite: `cd build && ninja -j4 && { ctest --output-on-failure > SP/kyo_feat_ctest.log 2>&1; echo "rc=$?" >> SP/kyo_feat_ctest.log; }`
       Expect `100% tests passed, 0 tests failed out of 206`, rc=0. AsmCallOracle*/LogicReplay* failure = STOP, report,
       no commit, no golden regeneration. Any other new failure also blocks.

    4. feat commit: `git add src/core/core.cpp test/test_remember_military_board_idx.cpp CMakeLists.txt include/jjffe/core/patch_gates.h jjffe.cfg`;
       `git diff --cached --name-only` lists exactly those 5. Message (HEREDOC):
       ```
       feat(patches): gate AndyJ RememberMilitaryBoardIdx

       AJ changelog L732-735, cfg 0x7a39d5 case 6, gate 0x7a5cfe, sole reader
       AJ 0x41b442 in FUNC_000493. With the flag on, the military board list
       reopens at the index last stored by FUNC_000355 instead of the
       FUNC_000342 reset, clamped as AJ's bytes: > count -> count, < 0 -> 1,
       else as-is. The AJ-new bss dword 0x899f6d becomes the port static
       s_aj899f6d (oracle label AJ_899f6d). Its ten stores (FUNC_000320,
       323, 324, 355, 490, 491, 493, 998) are ungated as in AJ; only the
       restore is gated, so the default build matches NASM. The
       ExtendedLifetime_HC_Military_Missions wrapper in the same function
       is a [PATCHES_FFE] key and is not ported. Adds
       RememberMilitaryBoardIdxTests.

       Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
       ```

    5. Docs (D5):
       a. docs/PATCH_INTEGRATION_PRIORITY.md 3E: header becomes
          `#### 3E. RememberMilitaryBoardIdx (AJ changelog L732-735; FINDING_7a's bytes, FINDING_8c mislabelled) — ✅ Complete`.
          Keep the Flag line, add "default 0 here" to it. Complexity ⭐⭐ Easy-Medium. FFE anchor: FUNC_000493 (ffe.asm
          L33367), restore block at JUMP_002274 (L33411, before L33412) and the empty-list write after L33409; AJ 0x41b442
          (fn 0x41b3b2); gate 0x7a5cfe, cfg 0x7a39d5, case 6. Replace "What to do" and the "Re-traced" bullet's
          "clamped to 0..[0x7eb1d2]" with an "As built" bullet: the reset is FUNC_000342's `DATA_009050 = 0`; with the flag
          on FUNC_000493 sets DATA_009050 from s_aj899f6d (AJ bss dword 0x899f6d, not saved) clamped as > count -> count,
          < 0 -> 1, else as-is (== count kept; count = DATA_009051 before the 0x983f trailer). A "Writers" bullet: AJ
          stores the index on every successful FUNC_000355 scroll (all console lists, so the mission sub-list FUNC_000508
          also overwrites it; AJ quirk, ported) and zeroes it in FUNC_000320 (param < 2), FUNC_000323, FUNC_000324 (beside
          each `DATA_009067 = 0`, i.e. the main BBS reset, changelog "new day"), FUNC_000490, FUNC_000491 (board entry),
          FUNC_000998 (game start) and on an empty list; all ten stores ungated, only the restore gated. A "Pre-existing
          fix" bullet: the invented FUNC_000355 empty-list guard was removed in a separate ungated fix(core) commit
          (ConsoleListScrollTests). A "Follow-up (NOT done)" bullet: AJ wraps the two FUNC_000034 calls in FUNC_000493 in
          `if (0x403779(0x63,0,0))` at 0x41b404 (same wrapper id 0x62 at 0x41a4b3); it returns 1 unless ExtendedLifetime
          is on, then gate 0x7a5d0e (ExtendedLifetime_HC_Military_Missions, [PATCHES_FFE], cfg 0x7a3a10) — a different
          key, not in PatchGates. Test bullet: `test/test_remember_military_board_idx.cpp`, ctest
          `RememberMilitaryBoardIdxTests` (flag off reset, flag on reuse, entry resets, clamp, gate at call time). Keep the
          FINDING_8c note.
          Week-3 list: item 16 becomes
          `16. ✅ RememberMilitaryBoardIdx (AJ changelog L732-735) — FUNC_000493 restores the FUNC_000355 scroll index`.
          Progress Tracker: after row 21 append
          `| 22 | RememberMilitaryBoardIdx | 3E | ✅ Complete | AJ changelog L732-735 / FINDING_7a bytes (gate 0x7a5cfe, reader 0x41b442); FUNC_000493 restores s_aj899f6d (AJ 0x899f6d) clamped >count -> count, <0 -> 1; ten ungated stores (FUNC_000320/323/324/355/490/491/493/998); plus ungated FUNC_000355 guard fix; follow-up: ExtendedLifetime_HC_Military_Missions wrapper 0x41b404 ([PATCHES_FFE]); test RememberMilitaryBoardIdxTests |`
       b. docs/ANDYJ_GATE_TABLE.md row L68 (RememberMilitaryBoardIdx (6)): site column -> `FUNC_000493 (L33367; restore at
          L33411, empty-list write after L33409)`; "what" column -> `gate -> DATA_009050 = AJ index 0x899f6d (stored by
          FUNC_000355 on scroll) clamped: > [0x7eb1d2] -> count, < 0 -> 1, else as-is; replaces FUNC_000342's reset`;
          last column -> `ported (quick 261122-kyo) as static s_aj899f6d; ten ungated writers`. No other rows.
       c. docs/ANDYJ_PATCH_INTEGRATION_STATUS.md, section "2. Move to conditional fixes", directly after the
          NoTurretMovementWhilePaused line, add
          `   - RememberMilitaryBoardIdx (AJ changelog L732-735; FINDING_7a's bytes) — FUNC_000493 restores the FUNC_000355 scroll index (AJ 0x41b442, gate 0x7a5cfe), clamped > count -> count, < 0 -> 1, done`
          and directly below it
          `   - ExtendedLifetime_HC_Military_Missions wrapper (AJ 0x41b404 / 0x41a4b3, call 0x403779) — follow-up, [PATCHES_FFE] key not in PatchGates, not part of RememberMilitaryBoardIdx`.
          Change no other rows or counts.
       Docs commit: `git add docs/PATCH_INTEGRATION_PRIORITY.md docs/ANDYJ_GATE_TABLE.md docs/ANDYJ_PATCH_INTEGRATION_STATUS.md`
       (exactly 3 cached files; NOT docs/ANDYJ_PATCHES_ANALYSIS.md). Message:
       ```
       docs(patches): RememberMilitaryBoardIdx ported; correct its clamp

       The bytes clamp > count -> count and < 0 -> 1, not 0..count. List all
       ten AJ writers of 0x899f6d, mark 3E complete, add tracker row 22, and
       record the ExtendedLifetime_HC_Military_Missions wrapper (AJ 0x41b404)
       as a [PATCHES_FFE] follow-up.

       Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
       ```

    6. Write `.planning/quick/261122-kyo-andyj-remembermilitaryboardidx/261122-kyo-SUMMARY.md` (NOT committed): the three
       hashes; totals 204 -> 205 -> 206; RED lines + rc (red_kyo_fix.log, red_kyo.log); any fixture adjustment or extra
       stub (expected: none beyond DATA_007639); whether FUNC_000320(0)'s DATA_005315 side effect mattered; AsmCallOracle
       / LogicReplay status; follow-ups (ExtendedLifetime wrapper; research A2 interpretation of ungated writes); optional
       in-game check (flag on, open the military board, scroll, open a mission, back -> same position).
  </action>
  <verify>
    <automated>cd /home/alex/Src/Reverse/games/jjffe && (cd build && ctest -N | python3 -c "import sys; assert 'Total Tests: 206' in sys.stdin.read()") && python3 - <<'EOF'
import subprocess, re
SP = "/tmp/claude-1000/-home-alex-Src-Reverse-games-jjffe/da890218-d44e-4cec-887f-b610155f4ef3/scratchpad"
def sh(*a): return subprocess.run(a, capture_output=True, text=True, check=True).stdout
f = open(f"{SP}/kyo_feat_ctest.log", errors="replace").read()
assert re.search(r"^rc=0$", f, re.M) and "100% tests passed, 0 tests failed out of 206" in f
subj = sh("git", "log", "-4", "--format=%s").splitlines()
assert subj[0].startswith("docs(patches)") and subj[1].startswith("feat(patches)") and subj[2].startswith("fix(core)") and subj[3].startswith("docs(quick-261122-kyo)"), subj
for r in ("HEAD", "HEAD~1", "HEAD~2"):
    assert sh("git", "log", "-1", "--format=%B", r).rstrip().endswith("Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"), r
names = lambda r: sorted(sh("git", "show", "--name-only", "--format=", r).split())
assert names("HEAD~1") == sorted(["CMakeLists.txt", "include/jjffe/core/patch_gates.h", "jjffe.cfg", "src/core/core.cpp", "test/test_remember_military_board_idx.cpp"])
assert names("HEAD") == sorted(["docs/ANDYJ_GATE_TABLE.md", "docs/ANDYJ_PATCH_INTEGRATION_STATUS.md", "docs/PATCH_INTEGRATION_PRIORITY.md"])
d = sh("git", "show", "-U0", "--format=", "HEAD~1", "--", "src/core/core.cpp").splitlines()
assert [l for l in d if l.startswith("-") and not l.startswith("---")] == ["-    param_1[0] = DATA_009051 == 0 ? 0x990b : 0x990a;"]
h = sh("git", "show", "-U0", "--format=", "HEAD~1", "--", "include/jjffe/core/patch_gates.h").splitlines()
assert not [l for l in h if l[:1] in "+-" and l[:3] not in ("+++", "---") and "bool " in l]
ph = open("include/jjffe/core/patch_gates.h").read(); assert len(re.findall(r"^    bool \w+ = false;", ph, re.M)) == 50 and "    bool RememberMilitaryBoardIdx = false;" in ph
assert open("src/core/patch_gates.cpp").read().count("COUNT_IF(") == 51
subprocess.run(["git", "diff", "--quiet", "66f82971", "HEAD", "--", "src/core/patch_gates.cpp", "test/test_patch_gates.cpp", "include/jjffe/data_globals.h", "include/jjffe/data_batch3_init.h", "src/core/logic_label_table.inc", "src/core/asm_label_table.inc", "src/core/services_tables.inc", "ffe.asm", "ffedat.asm", "data_blob", "src/game"], check=True)
b = open("jjffe.cfg", "rb").read(); assert b.count(b"\n") == 113 and b.count(b"\r\n") == 113
ls = b.split(b"\r\n"); i = ls.index(b"RememberMilitaryBoardIdx=0"); c = ls[i - 1]
assert c.startswith(b"# RememberMilitaryBoardIdx ") and b"=" not in c and b"[" not in c
pr = open("docs/PATCH_INTEGRATION_PRIORITY.md").read(); gt = open("docs/ANDYJ_GATE_TABLE.md").read(); st = open("docs/ANDYJ_PATCH_INTEGRATION_STATUS.md").read()
for t in (pr, gt): assert "0..[0x7eb1d2]" not in t
assert re.search(r"^#### 3E\. RememberMilitaryBoardIdx .*✅ Complete", pr, re.M)
assert "0x41b404" in pr and "ExtendedLifetime_HC_Military_Missions" in pr and "FUNC_000998" in pr and "FUNC_000355" in pr
assert "16. ✅ RememberMilitaryBoardIdx (AJ changelog L732-735)" in pr and re.search(r"^\| 22 \| RememberMilitaryBoardIdx \| 3E \| ✅ Complete \|", pr, re.M)
row = next(l for l in gt.splitlines() if l.startswith("| RememberMilitaryBoardIdx (6)")); assert "< 0 -> 1" in row and "261122-kyo" in row
assert re.search(r"RememberMilitaryBoardIdx \(AJ changelog L732-735.*done", st) and "0x41b404" in st
assert "?? docs/ANDYJ_PATCHES_ANALYSIS.md" in sh("git", "status", "--porcelain")
print("task3 OK")
EOF</automated>
  </verify>
  <done>
    Full ctest 206/206 (AsmCallOracle*/LogicReplay* green, no golden touched). feat(patches) commit with 5 files (one
    removed core.cpp line = the ternary; patch_gates.h comment only; cfg 113/113 CRLF with one comment line), then a
    docs(patches) commit with the 3 docs files. PatchGates still 50/51. ANDYJ_PATCHES_ANALYSIS.md untracked. SUMMARY
    written, not committed.
  </done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| jjffe.cfg -> PatchGates | user-editable cfg selects the flag (existing loader, unchanged) |
| console list state -> FUNC_000493 | remembered index s_aj899f6d (written on every list scroll) becomes DATA_009050 |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-kyo-01 | T (parity drift) | default build | mitigate | Only the restore block is gated; flag-off test groups 1/5 pass on the unpatched (RED) and patched core; the static is outside g_bss8804, so AsmCallOracle probe 491 and LogicReplay guard flag-off parity; removed-line sets pinned in verify. |
| T-kyo-02 | T (behaviour change in default build) | FUNC_000355 guard removal | accept | Restores ffe.asm L27550-27570 exactly; ungated by design (pre-existing port bug), own commit + ConsoleListScrollTests. |
| T-kyo-03 | D (out-of-range index) | FUNC_000493 clamp | mitigate | Byte-exact clamp keeps idx in [0, count] (negative -> 1, > count -> count); test group 4 covers -4, 7, == count and empty list. |
| T-kyo-04 | T (test-only hook) | OracleLabelStorageTable AJ_899f6d | accept | Name-keyed lookup only; 179 of 256 entries, no production reader. |
| T-kyo-05 | R (scope) | ExtendedLifetime wrapper 0x41b404 | accept | Different [PATCHES_FFE] key; recorded as follow-up in PRIORITY 3E and STATUS (D3). |
</threat_model>

<verification>
- fix(core): RED rc 134 on `FUNC_000355(1) == 1`; diff = the deleted guard; ctest 205/205.
- feat(patches): RED rc 134 on the first flag-on assert, no missing label; GREEN with one removed line (the ternary),
  8 ungated write sites + 2 clamp stores, one gate read; ctest 206/206; PatchGates 50/51; generated files untouched.
- docs(patches): clamp wording corrected everywhere it was wrong, writer list, ExtendedLifetime follow-up, 3E complete,
  tracker row 22.
</verification>

<success_criteria>
- Three commits on 66f82971 in order fix(core), feat(patches), docs(patches), explicit-path staged, with the trailer.
- ConsoleListScrollTests and RememberMilitaryBoardIdxTests added and passing (204 -> 206), AsmCallOracle*/LogicReplay*
  green.
- Flag off == NASM; flag on reopens the military board at the remembered, byte-exactly clamped index.
- SUMMARY written, not committed.
</success_criteria>

<output>
After completion, create `.planning/quick/261122-kyo-andyj-remembermilitaryboardidx/261122-kyo-SUMMARY.md`
</output>

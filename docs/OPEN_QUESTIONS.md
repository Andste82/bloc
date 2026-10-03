# Open questions

Entries follow section 1.3 of `docs/IMPLEMENTATION_PLAN.md`: the section concerned, the question,
the interpretation that was chosen and the tests it affects.

## OQ-001 CMake cache entries created by `project(bloc VERSION ...)`

- **Section:** implementation plan 3.8 (CM-04, CM-09) and 9.8 (FC-02).
- **Question:** CM-09 requires `project(bloc VERSION X.Y.Z)`. FC-02 requires that every cache entry
  that is new after `FetchContent_MakeAvailable(bloc)` matches `^(BLOC_|bloc_|FETCHCONTENT_)`.
  CMake itself records the version of a sub-project in the STATIC cache entries
  `CMAKE_PROJECT_VERSION`, `CMAKE_PROJECT_VERSION_MAJOR`, `_MINOR`, `_PATCH` and `_TWEAK` when the
  consumer's own `project()` call has no `VERSION` (observed with CMake 4.2). The two rules
  therefore contradict each other for such a consumer.
- **Chosen interpretation:** keep `project(bloc VERSION ...)` (CM-09), and let FC-02 additionally
  accept `CMAKE_PROJECT_VERSION*`. These entries are written by CMake, not by BLOC's CMake code,
  and BLOC cannot avoid them without dropping its version.
- **Tests affected:** FC-02 (every variant of `scripts/fetchcontent_smoke.sh`).

## OQ-002 Runtime invariant check in `bloc_release` (DBG-10)

- **Section:** spec 13 (check table, "`active_count <= element_count` after alloc and release"),
  implementation plan 4.5 (`bloc_release`) and 8.10 (DBG-10).
- **Question:** DBG-10 says to corrupt `active_count` to `element_count + 1` before a final release
  and expects one assertion. The check runs after the decrement, so the value seen by the check is
  `element_count` and holds the invariant: no assertion would fire.
- **Chosen interpretation:** the check stays as specified (after the decrement). The test corrupts
  `active_count` to `element_count + 2`, so that the value after the final release is
  `element_count + 1` and the invariant is violated.
- **Tests affected:** DBG-10 (`test/test_debug.c`). ALLOC-17 is not affected: `bloc_alloc` checks
  after the increment, and `active_count = element_count` is corrupt enough.

## OQ-003 Overflow of the rounded headroom in `bloc_alloc`

- **Section:** spec 8 (`bloc_alloc`, step 2), implementation plan 4.5.
- **Question:** step 2 computes `align_up(headroom, PA)` "in `size_t`" and compares it with
  `element_size`. With a 32-bit `size_t` and `BLOC_SIZE_T = uint32_t` (configuration `wide` on i386,
  armhf, powerpc), `headroom = BLOC_SIZE_MAX` makes `headroom + PA - 1` wrap, the rounded value
  becomes small and the allocation would wrongly succeed.
- **Chosen interpretation:** the observable behaviour of the spec is kept (NULL if the rounded
  headroom exceeds `element_size`; nothing taken, nothing counted, no lock). It is evaluated as
  `headroom > (element_size rounded down to PA)`, which is equivalent and cannot overflow. The
  rounding is only done after that check passed.
- **Tests affected:** ALLOC-02, ALLOC-03 (including `BLOC_SIZE_MAX` and an element size that is not
  a multiple of PA), ALLOC-11.

## OQ-004 Undefined symbols that the plan's allow-lists did not foresee (XC-02, NH-01..02, EM-03)

- **Section:** spec 2 and 6, implementation plan R-03, 9.1, 9.4, 9.6.
- **Question:** the checks allow only `memcpy`, `memset` (plus division helpers in debug builds).
  The library objects of the following builds reference other symbols, none of which is a heap or
  C library call:
  1. Clang for ARM EABI turns a `memset` call into `__aeabi_memclr`.
  2. `__builtin_trap()`, the default assertion of spec 6, compiles to a call to `abort()` on AVR,
     which has no trap instruction.
  3. avr-gcc marks every object with string literals (the assertion messages of a debug build)
     with the startup symbol `__do_copy_data`, because AVR keeps them in RAM.
  4. GCC for PowerPC used the libgcc epilogue helper `_restgpr_30_x` at `-Os` for `bloc_calloc`,
     which kept the pool and the block across the `memset` call. No compiler option turns it off.
  5. The EM-03 row "debug with default assert" did not allow the division helpers that R-03 allows
     for `BLOC_DEBUG = 1`, so armhf (no hardware divider in ARMv7-A) failed it.
- **Chosen interpretation:** keep the gates, change the cause where the code or the flags can: (1)
  `-fno-builtin-memset` for Clang, PRIVATE to the library target and in
  `bloc_library_warning_flags`; (2) the default assertion of `bloc_opt.h` uses the infinite loop
  variant on AVR, which is the "no C library" intent of spec 6 (this changes a public header and
  deviates from the literal text of spec 6 and R-04, so it needs the owner's confirmation); (4)
  `bloc_calloc` shares the body of `bloc_alloc` and recovers the handle from the return value of
  `memset`, so that no value lives across the call and the helper is no longer emitted (the
  `powerpc` gate is strict again); the handle validity checks of debug builds are function-like
  macros (`BLOC_I_ADDR_VALID`, `BLOC_I_HANDLE_VALID`) and not functions, for the same reason: an
  out-of-line helper keeps the handle alive across a call and brings `_savegpr_*`/`_restgpr_*`
  back, and R-04 forbids the inline attributes; (5) the debug row now passes the division helpers. For (3),
  which no flag or source change avoids, `__do_copy_data` is accepted narrowly: only for the AVR
  target, only for a debug build (`BLOC_DEBUG = 1`) and only when its configuration header uses
  the `ts_assert_fail` test hook,
  because that hook is what turns the assertion messages into string literals (spec 17 names them
  as the only `.rodata` of a debug build). The AVR default-assert rows stay strict.
- **Tests affected:** XC-02, XC-03 (all AVR debug rows, cm0plus/cm3 Clang rows), EM-03 (armhf,
  powerpc), NH-02. Phase 4 must check whether Clang also needs `-fno-builtin-memcpy`
  (`__aeabi_memcpy`) once `memcpy` is used.

## OQ-005 Deviations from the literal text of the plan in phase 2

- **Section:** implementation plan 4.4, 4.5, 9.8, ALLOC-08.
- **Question and chosen interpretation:**
  1. Smoke `main.c`, check 2 compares the block distance with `BLOC_HEADER_SIZE +
     BLOC_ALIGN_UP(8, PA)` instead of the literal `HEADER_SIZE + 8`: with `PA = 16` in
     `smoke_opts.h` the literal contradicts the plan's own configuration.
  2. ALLOC-08 did not call `bloc_set_len` while that function did not exist. Resolved: the test
     now sets the length to the element size and checks it.
  3. `bloc_i_overlaps` (plan 4.4) is not present yet. It would be dead code and break the 100 %
     function coverage gate until the phase that uses it (phase 4).
  4. `bloc_i_addr_valid` uses `/` and `%` in debug builds where the plan names `%` only. R-03 allows
     the unsigned division helpers for `BLOC_DEBUG = 1`, and the result is mathematically
     equivalent to steps 4 and 5 (the distance is divided instead of multiplying count and stride,
     which would need a multiplication helper on RV32I).
- **Tests affected:** FC-01 (smoke check 2), ALLOC-08, DBG-* (handle validity), EM-03, XC-02 (debug
  rows).

## OQ-006 Deviations from the literal text of the plan in phase 4

- **Section:** implementation plan 4.4, 4.5, 4.6, 9.4, 9.5 and the OQ-004 follow-up.
- **Question and chosen interpretation:**
  1. Clang for ARM EABI lowers `memcpy` to `__aeabi_memcpy`, as OQ-004 predicted. `-fno-builtin-memcpy`
     is added next to `-fno-builtin-memset` (library target only, PRIVATE, and in
     `bloc_library_warning_flags`). No gate was loosened.
  2. `bloc_i_overlaps` (plan 4.4) is the function-like macro `BLOC_I_OVERLAPS`, for the reason given
     for `BLOC_I_ADDR_VALID` in OQ-004: an out-of-line helper keeps the handle alive across a call.
  3. The sketches of plan 4.5 update `len` and `offset` after `memcpy`. With that order GCC for
     PowerPC emits `_restgpr_30_x` at `-Os` in every copy, append and prepend function (EM-03 failed).
     The functions now compute both pointers, update the destination fields and then call `memcpy`
     as the last action. This is not observable: all checks have passed by then and `memcpy` cannot
     fail. For `bloc_prepend(b, b, n)` the source pointer is still computed before `offset` changes.
  4. FP-04: the host x86-64 rows are gated like every other row (FP-03 and the baseline), but the
     baseline is only compared when the compiler version matches the recorded one, which differs
     between machines. Rows of all targets are in `scripts/size_baseline.txt`.
  5. Size-reduction pass (plan 4.6): per-function sizes on ARMv6-M and ARMv7-M were compared (the
     largest of the seven new functions is `bloc_append` with 64 bytes on cortex-m0plus). Sharing an
     internal routine between the BLOC-source and the external-source variants would put the bounds
     check and the shared-mutation check of the two variants into one function, but the debug
     overlap check must run between them (spec 13, check order), so the variants cannot share it
     without a mode argument that FP-05 forbids. No structural change was found that reduces the
     size, and both budgets are met with a wide margin (cortex-m0plus default 860 of 1536 bytes,
     nochecks 626 of 1152; cortex-m3 default 812 of 1280, nochecks 618 of 960), so the baseline was
     created from the first measured values.
- **Tests affected:** XC-02, XC-03 (Clang rows), EM-03 (powerpc), FP-02, FP-04, DBG-08.

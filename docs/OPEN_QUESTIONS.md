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

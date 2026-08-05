---
name: raidline-build-test-ci
description: Configure, build, test, and diagnose Project Raidline on Windows or Ubuntu using the repository's current CMake presets, vcpkg manifest, CTest registrations, Python asset tests, and GitHub Actions workflow. Use for local verification, CI failures, compiler or linker diagnostics, missing test discovery, or environment-versus-code classification.
---

# Raidline Build, Test, and CI

## Read live configuration first

Read `doc/engineering/BUILD_AND_TEST.md`, `CMakePresets.json`, `CMakeLists.txt`, `vcpkg.json`, and `.github/workflows/ci.yml` before choosing commands. Treat the documents as guidance and the executable configuration as final authority.

Inspect `git status`, tool versions, environment variables, generator/compiler discovery, and the selected build directory. Never fix a missing compiler by hard-coding a user-specific path into shared project files.

## Run checks from narrow to broad

1. Configure the intended preset or reproduce the workflow's explicit CMake command.
2. Build the affected target when available, then build all targets.
3. Run the smallest relevant CTest regex or executable with `--output-on-failure`.
4. Run full CTest.
5. Run applicable standalone Python checks, especially `tests/test_phase1_assets.py`, because they are not registered in CTest.
6. Inspect GitHub Actions for the exact commit when remote verification is in scope.

Use `ctest -N` and `compile_commands.json` to prove that a new test is registered and a new source is actually compiled. A green suite does not validate source omitted from CMake.

## Diagnose by layer

Classify the first causal failure as one of:

- toolchain or environment discovery;
- dependency/vcpkg baseline or system package;
- configure/generator;
- compile/include/type contract;
- link/missing definition or target source;
- test assertion or discovery;
- runtime/resource/manual UI;
- CI-only platform or workflow behavior.

Preserve the shortest useful failing command and diagnostic. Avoid treating cascaded compiler errors as independent causes. Compare local and workflow compiler, generator, triplet, build type, and manifest-install behavior before labeling a failure CI-only.

## Report evidence

Return exact commands, configure/build result, focused and full test counts, standalone check results, commit-specific CI state, skipped checks, and failure classification. Mark manual visual or interaction checks `未验证` until a person performs them.

Do not change product behavior while verifying. Send source fixes to the primary thread or `raidline-implementer`.

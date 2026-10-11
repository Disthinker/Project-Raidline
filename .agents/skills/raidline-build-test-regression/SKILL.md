---
name: raidline-build-test-regression
description: Run or diagnose Raidline builds, regression tests and exact-revision CI; not documentation wording changes.
---

# Build, Test and Regression

Inspect the diff first. Use the setup/commands in doc/engineering/BUILD_AND_TEST.md; read presets, dependency manifest or workflow only when changing or diagnosing that layer. Historical run logs are not current commands.

On Windows, initialize the configured VS Developer Shell (x64 host/target), restore the repository working directory, set UTF-8 and VCPKG_ROOT, then use windows-debug.

- Incremental build and focused tests during implementation; reconfigure if CMake inputs change.
- Rebuild all affected targets after headers/class layout change. For gtest_ar_ stack corruption or source/binary disagreement, inspect Ninja dependencies and object timestamps before rerunning.
- Root AGENTS.md requires full CTest before code push and exact-head Windows/Ubuntu CI for C++ PRs. Do not repeat a complete cycle for each internal edit.
- For instruction/docs-only changes, validate affected formats, links and loading instead of unrelated C++ tests.
- Art-pipeline tests apply only to authorized art work; inspect their generated-output behavior first.

Return exact revision, commands/preset/targets, counts and failures, CI identity when applicable, and skipped checks with reasons. Do not count stale binaries or static checks as gameplay acceptance.

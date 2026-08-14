---
name: raidline-build-test-regression
description: Configure, build, test, diagnose, and report Project Raidline local and CI regression evidence.
---

# Raidline Build Test and Regression

## Use the supported Windows baseline

Enter the configured Visual Studio Developer Shell with x64 host/x64 target, restore the repository working directory after shell initialization, set UTF-8 code page and `VCPKG_ROOT`, then use the `windows-debug` preset.

## Match evidence to risk

- Run focused tests while implementing and full CTest before push.
- Reconfigure when CMake inputs change.
- Rebuild every affected target when headers or class layout change.
- Run Python asset tests only for an explicitly authorized asset-pipeline task.
- Require exact-head Windows and Ubuntu CI for C++ PRs.

## Diagnose stale builds

If behavior disagrees with source or MSVC/GTest reports `gtest_ar_` stack corruption, inspect Ninja dependencies and object timestamps, rebuild affected targets, and rerun. Never count an old binary as evidence for new code.

## Report concisely

Record commit, preset, targets, registered test count, passed/failed count, duration, CI URLs/status, and any untested risk.

---
name: raidline-build-test-regression
description: Configure, build, test, diagnose, and report Project Raidline local and CI regression evidence.
---

# Raidline Build Test and Regression

## Use the supported Windows baseline

Enter the configured Visual Studio Developer Shell with x64 host/x64 target, restore the repository working directory after shell initialization, set UTF-8 code page and `VCPKG_ROOT`, then use the `windows-debug` preset.

## Match evidence to risk

- Use CTest labels instead of guessing target lists: `sentinel`, `area-*`, `layer-integration` and `long-sequence`.
- Run focused area tests while implementing, Sentinel before and after authoritative edits, and full CTest before pushing tracked C++/CMake changes.
- Reconfigure when CMake inputs change.
- Rebuild every affected target when headers or class layout change.
- Run Python asset tests only for an explicitly authorized asset-pipeline task.
- Require exact-head Windows and Ubuntu CI for C++ PRs.
- Run `python tools/raidline_governance.py postflight --envelope <task.toml> --run-tests` for the final local evidence gate.

## Diagnose stale builds

If behavior disagrees with source or MSVC/GTest reports `gtest_ar_` stack corruption, inspect Ninja dependencies and object timestamps, rebuild affected targets, and rerun. Never count an old binary as evidence for new code.

## Report concisely

Record commit, preset, targets, label counts, registered/full passed/failed count, duration, architecture/postflight result, CI URLs/status and any untested risk. Do not copy transient CI state into stable project documents.

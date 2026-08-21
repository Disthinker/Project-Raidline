# 领域上下文：构建、测试与交付

## 权威与入口

- `CMakeLists.txt` 定义四个生产库及每个 production cpp 的唯一编译所有者。
- `.github/workflows/ci.yml` 是 exact-head Windows/Ubuntu 门；`doc/engineering/BUILD_AND_TEST.md` 只保存稳定命令。
- `tools/raidline_governance.py` 提供 route、snapshot、preflight、impact、architecture 与 postflight。

## 测试层级

- Sentinel：`ctest -L sentinel`，快速保护最高价值不变量。
- Area：`ctest -L area-<name>`，按领域选择。
- Integration：`ctest -L layer-integration`，覆盖 GameSession/GameplayWorld/Alpha 流程。
- Full：完整 CTest；C++ PR 还需 exact-head Windows/Ubuntu CI。
- Long sequence：`ctest -L long-sequence`，用于 authority 风险。

## 核心规则

- source/header/CMake 变化要重建影响目标；推送 C++ PR 前全量 CTest。
- CMake 配置自动保护唯一源码所有者和 domain/simulation SDL-free；治理架构检查再保护向上 include 与 Projectile 回归。
- 一个任务只有一个业务 writer；Explorer/Reviewer 只读，Verifier 不修改 tracked source。
- 人工验收只证明玩家可见行为，统一放在自动化与 CI 后由用户执行。

## 主要命令

```powershell
python tools/raidline_governance.py preflight --task "<task>" --envelope <task.toml> --run-sentinel
python tools/raidline_governance.py postflight --envelope <task.toml> --base origin/main --run-tests
ctest --test-dir build/windows-debug -L area-combat --output-on-failure
```

## 跨域风险与禁止事项

修改测试目录、标签、CMake 或 CI 会影响所有后续切片，应使用至少 `cross-domain` 风险门。不得把旧构建产物当新证据，不得为了记录临时 CI/PR 状态机械修改稳定项目文档。

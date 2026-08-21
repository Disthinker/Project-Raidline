# Project Raidline 构建与测试

本文只保存稳定命令和诊断规则。当前测试数量、提交和 CI 状态由 `tools/raidline_governance.py snapshot` 与 GitHub 负责。

## Windows 支持环境

- Visual Studio：`E:\WorkPlace\VS 2022\IDE`
- Developer Shell：`E:\WorkPlace\VS 2022\IDE\Common7\Tools\Launch-VsDevShell.ps1`
- `VCPKG_ROOT`：`E:\WorkPlace\VS 2022\IDE\VC\vcpkg`
- Preset：`windows-debug`；Ninja / Debug / `x64-windows`；UTF-8。

```powershell
& 'E:\WorkPlace\VS 2022\IDE\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath 'E:\WorkPlace\Projects\C\Project RaidLine'
chcp 65001
$env:VCPKG_ROOT = 'E:\WorkPlace\VS 2022\IDE\VC\vcpkg'

cmake --preset windows-debug
cmake --build --preset windows-debug --parallel
ctest --test-dir build/windows-debug --output-on-failure -j 8
```

Developer Shell 可能改变当前目录，因此初始化后必须显式返回仓库。

## 分层测试

每个 GoogleTest target 都必须在 `cmake/RaidlineTestCatalog.cmake` 声明 layer/area 标签；缺标签会使 CMake configure 失败。

```powershell
# 最高价值不变量，适合新会话快速健康检查
ctest --test-dir build/windows-debug -L sentinel --output-on-failure -j 8

# 领域示例
ctest --test-dir build/windows-debug -L area-combat --output-on-failure -j 8
ctest --test-dir build/windows-debug -L area-inventory --output-on-failure -j 8
ctest --test-dir build/windows-debug -L area-persistence --output-on-failure -j 8

# 跨域/长期
ctest --test-dir build/windows-debug -L layer-integration --output-on-failure -j 8
ctest --test-dir build/windows-debug -L long-sequence --output-on-failure -j 8
```

层级用途：

- `layer-contract` / `layer-domain`：权威值、命令和局部模拟。
- `layer-integration`：GameSession、GameplayWorld、Alpha 生命周期。
- `layer-client`：输入、UI 和音频适配。
- `sentinel`：重复注册的一小组既有高价值合同；不替代领域或全量回归。
- `long-sequence`：authority 风险的连续多局/重载验证。

最终门由任务风险决定；tracked C++/header/CMake 变化在 push 前仍需全量 CTest，C++ PR 需要 exact-head Windows/Ubuntu CI。玩家可见行为由用户最后正常游玩验收；开发代理不启动游戏替代。

## 统一治理入口

```powershell
python tools/raidline_governance.py snapshot
python tools/raidline_governance.py architecture
python tools/raidline_governance.py preflight --task "<task>" --envelope <task.toml> --run-sentinel
python tools/raidline_governance.py postflight --envelope <task.toml> --base origin/main --run-tests
```

Preflight 生成忽略的 `build/governance/project-snapshot.json`；postflight 检查实际影响面、Task Envelope、架构、diff 和风险对应测试。

## 重新构建与 stale binary

- CMake/测试目录变化：重新 configure。
- 头文件或类布局变化：重建所有消费者，不能只看单个 target。
- 如果 `Project_Raidline.exe` 正在运行，Windows 链接可能报 `LNK1168`；不要擅自终止用户进程，关闭游戏后重跑全目标。
- 如果 MSVC/GTest 出现 `gtest_ar_` 栈损坏或源码与行为不符，检查 Ninja `/showIncludes` 依赖和对象时间戳，重建受影响目标；不得把旧二进制当新证据。
- 美术管线测试只有在命名资源包被明确授权后才能运行；相关命令可能写 QA 输出。

交付证据记录：分支/提交、Preset、configure、构建目标、各 label 与 full 通过/失败数、architecture/postflight、exact-head CI、人工验收（如适用）和残余风险。

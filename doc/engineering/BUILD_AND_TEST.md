# Project Raidline 构建与测试

最后核对：2026-08-14。

## Windows 支持环境

- Visual Studio：`E:\WorkPlace\VS 2022\IDE`
- Developer Shell：`E:\WorkPlace\VS 2022\IDE\Common7\Tools\Launch-VsDevShell.ps1`
- `VCPKG_ROOT`：`E:\WorkPlace\VS 2022\IDE\VC\vcpkg`
- Preset：`windows-debug`
- Generator/配置/目标：Ninja / Debug / `x64-windows`
- 代码页：UTF-8

Developer Shell 初始化会改变当前目录，因此必须显式返回仓库：

```powershell
& 'E:\WorkPlace\VS 2022\IDE\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64
Set-Location -LiteralPath 'E:\WorkPlace\Projects\C\Project RaidLine'
chcp 65001
$env:VCPKG_ROOT = 'E:\WorkPlace\VS 2022\IDE\VC\vcpkg'
```

## 配置、构建与测试

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --test-dir build/windows-debug --output-on-failure -j 8
```

当前 `codex/core-extraction-alpha-slice-0` 的精确代码提交 `d046753` 在重新配置和完整增量构建后注册 550 个 CTest，550/550 通过。射击/命中/GameplayWorld/GameSession/GameFlow 专项 102/102 通过；GitHub 范围检测、Windows 和 Ubuntu CI 全部成功。真实窗口与手感验收由用户在自动化完成后统一执行，开发代理不自行打开游戏，也不把进程存活冒烟当作人工验收。

`codex/rl-inv-003-ammo-stack-merge` 吸收 `origin/main@c7a3931` 后重新配置并完成 55 步增量全目标构建；RL-INV-003 精确新增行为 8/8、广义库存/鼠标 37/37、全量 CTest 558/558 通过。精确 merge head `0523b3d` 的范围检测、Windows 和 Ubuntu CI 全部成功。

`codex/build-module-foundation` 从 `origin/main@5bbddc3` 重新配置并完成 68 步全目标构建。四个生产库生成成功，31/31 个非 main 业务 `.cpp` 各有且仅有一条生产库编译规则；跨层 focused 133/133、全量 CTest 558/558 通过。该分支不改变可见行为，人工窗口验收不作为门禁；精确 head CI 尚待 PR 推送后完成。

## 按风险选择证据

- 修改纯领域类：构建对应测试目标，跑 focused tests，再跑全量 CTest。
- 修改头文件或类布局：重建所有消费者；不能只看单个测试目标。
- 修改 CMake：重新运行 `cmake --preset windows-debug` 后构建。
- 修改 App/SDL 投影：构建主程序，跑领域/流程回归，并做真实窗口或至少进程启动冒烟。
- 修改资产管线：只有艺术包重新授权后，才运行相应 `tools/art_pipeline` 与 `tests/test_phase1_assets.py`；这些命令可能写 QA 输出，运行前先确认边界。

## stale binary 与 MSVC 头依赖

CMakeLists 中保留了 MSVC 中文 `/showIncludes` 前缀修正。若出现源码与行为不一致或 `gtest_ar_` 栈损坏：

1. 检查 Ninja 是否记录受影响头文件依赖。
2. 检查目标对象时间戳与编译命令。
3. 重建全部消费者，必要时只删除明确受影响的对象/目标后重建。
4. 重新运行 focused/full tests。

不得把旧二进制结果当作新代码证据，也不得用破坏性清理替代依赖诊断。

## 证据记录格式

每次交付记录：分支与提交、Preset、重新配置与否、构建目标、registered/passed/failed 数、focused tests、启动/真实窗口验收、Windows/Ubuntu CI、未验证风险。

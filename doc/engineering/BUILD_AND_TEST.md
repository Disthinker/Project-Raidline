# Project Raidline 构建与测试

最后核对：2026-08-15。

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

`codex/build-module-foundation` 从 `origin/main@5bbddc3` 重新配置并完成 68 步全目标构建。四个生产库生成成功，31/31 个非 main 业务 `.cpp` 各有且仅有一条生产库编译规则；跨层 focused 133/133、全量 CTest 558/558 通过。最终 feature head `ef66dbd` 的范围检测、Windows、Ubuntu CI 全部成功，PR #56 已以 merge commit `1837928` 进入 main。该迁移不改变可见行为，人工窗口验收不适用。

`codex/content-registry-v1` 从 `origin/main@1837928` 引入锁定的 `nlohmann-json` 3.12.0 header-only overlay，绕开旧 VS vcpkg 对已下线 MSYS2 runtime 的 pkg-config 下载，不更新其他依赖。Windows Debug 重新配置和 70 步全目标构建成功；DefinitionId/Registry/物品/Loot/GameplayWorld/GameSession/GameFlow focused 134/134、全量 CTest 574/574 通过。发布资源逻辑引用与物理文件存在性均由自动化覆盖；Draft PR #57 实现/初始证据 head `3a12385` 的范围检测、Windows、Ubuntu CI 全部成功。本迁移不改变可见行为，开发代理不启动游戏，人工窗口验收不适用。

`codex/core-alpha-persistent-base` 从已包含 PR #57 的 `origin/main@14cf79b` 重新配置并完成 120 步全目标构建。Content/Profile/Inventory/Economy/Base/Persistence/PersistentSession/Input focused 70/70、存档恢复专项 9/9、全量 CTest 601/601 通过，0 失败；精确 head CI 与用户 6/6 真实窗口验收通过，PR #58 已以 merge commit `b1ea3c3` 进入 main。开发代理未启动游戏。

`codex/core-alpha-extraction-loop` 从 `origin/main@b1ea3c3` 创建。领域合同提交 `2d9b96d` 与端到端实现提交 `66f3120` 已通过 Windows Debug 构建；InventoryDomain/RaidLifecycle/AlphaExtractionSession focused 17/17、全量 CTest 620/620 通过，0 失败。PR #59 head `0b495f7` 的范围检测、Windows 与 Ubuntu CI 全部成功。用户使用当前 Windows Debug 可执行文件完成第 8 节集中真实窗口验收，7/7 通过且未报告偏差；开发代理未启动游戏。

`codex/core-alpha-hardening` 从已包含 PR #59 的 `origin/main@ed45baa` 创建。Windows Debug 全目标重建成功；库存/角色返工、Raid 压卸弹、进程退出回滚、奔跑和空栏位快速装备的全量 CTest 645/645 通过，0 失败；最新快捷转移/快速装备 head 等待精确 CI。自动化覆盖 10 局混合结果、至少 3 次重载、三配置/三路线、出击前精确回滚、旧 pending 迁移、双损坏存档、Deploy 保存失败、Raid 动作中断、弹药守恒及快速装备的查询不变性；既有功能 head `db0935d` 的范围检测、Windows 和 Ubuntu CI 全部成功（run `31919983014`）。`Project_Raidline.exe` 已生成但开发代理未启动游戏；最终人工验收由用户执行。

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

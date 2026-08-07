# Project Raidline 构建、测试与 CI

命令以当前 `CMakePresets.json`、`CMakeLists.txt`、`vcpkg.json` 和 `.github/workflows/ci.yml` 为最终事实来源。工具链或 workflow 变化时先更新配置，再更新本文件。

## Windows 前置条件

- Visual Studio C++ x64 工具链（`cl.exe`）已进入当前 Developer PowerShell 环境。
- CMake 与 Ninja 在 `PATH`；可使用 Visual Studio 自带版本。
- `VCPKG_ROOT` 指向含 `scripts/buildsystems/vcpkg.cmake` 的 vcpkg 根目录。

本机当前安装位置是 `E:\VisualStudio\VC\vcpkg`，但不要把该用户路径写入共享 CMake 配置。推荐打开 “Developer PowerShell for VS 2022” 后运行：

```powershell
$env:VCPKG_ROOT = 'E:\VisualStudio\VC\vcpkg'
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

如果当前是普通 PowerShell，可先在同一进程加载开发环境：

```powershell
& 'E:\VisualStudio\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
```

中文 MSVC + Ninja 使用 `/showIncludes` 追踪头文件。配置和构建必须使用一致的控制台代码页；本机统一为 UTF-8：

```powershell
chcp 65001
```

仓库的 CMake 配置会纠正 CMake 3.31 已知的中文前缀乱码检测，但同一 `build/windows-debug` 不应交替由不同代码页的进程增量构建。

这个路径只用于本机命令示例，不得写入共享 CMake 配置。若未加载 Developer Shell，已有 Ninja cache 仍可能找到 `cl.exe`，但编译会因缺少 MSVC 标准库 `INCLUDE` 而报 `<optional>`、`<array>` 等文件不存在；此时旧 CTest 二进制不属于新代码的验证证据。

Preset 使用 Ninja、Debug、`x64-windows`，构建目录是 `build/windows-debug`。如果 CMake 报找不到 compiler 或 Ninja，先修复 Developer Shell/PATH；如果 toolchain 变成 `/scripts/buildsystems/vcpkg.cmake`，说明 `VCPKG_ROOT` 为空。

## 定向构建与测试

先构建并运行最小相关目标，再做全量：

```powershell
cmake --build --preset windows-debug --target RaidSessionTest ExtractionPointTest LootTableTest GridInventoryTest InventoryTransferTest StorageCabinetTest GameplayWorldTest InventoryInteractionTest MouseInventoryInteractionTest
ctest --preset windows-debug -R '^(RaidSessionTest|ExtractionPointTest|GameplayWorldRaidTest|LootTableTest|GridInventoryTest|InventoryTransferTest|StorageCabinetTest|GameplayWorldTest|GameplayWorldLootTest|InventoryInteractionTest|InventoryOverlayStateTest|InventoryContainerInteractionTest|InventoryFrameInputArbitrationTest|MouseInventoryLayoutTest|MouseInventoryInteractionTest|MouseInventoryIntegrationTest|MouseInventoryFrameArbitrationTest)\.'
ctest --preset windows-debug
```

显示注册用例而不执行：

```powershell
ctest --test-dir build/windows-debug -N
```

定位新源是否实际编译：

```powershell
rg 'inventory.interaction' build/windows-debug/compile_commands.json
```

当前 CMake 注册 23 个 GTest executable、416 个 CTest 用例。`RaidSessionTest` 覆盖六态转换、连续撤离、离开取消、超时/撤离竞态、死亡与 sticky 终局；`ExtractionPointTest` 覆盖有限几何和半开边界；`GameplayWorldRaidTest` 覆盖移动后占用、撤离、默认超时和终局冻结。`LootTableTest` 覆盖权重/数量边界、堆叠规范化、非法表和随机源契约；`GameplayWorldLootTest` 覆盖首次搜索、重开不重抽、取空不刷新、稳定 ID 与失败快照。`MouseInventoryInteractionTest` 编译 canonical `src/inventory_interaction.cpp` 与真实 `GridInventory`，覆盖布局、帧级输入仲裁、平滑像素拖拽、旋转锚点和同/跨容器事务集成；`InventoryTransferTest` 覆盖整栈 first-fit、同/跨容器指定格精确数量放置、稳定合并顺序、拆分 ID 和失败回滚。可用下列命令证明 Raid、Loot 与鼠标测试源真实进入编译数据库：

```powershell
rg 'test_mouse_inventory_interaction.cpp' build/windows-debug/compile_commands.json
rg 'loot_table.cpp|test_loot_table.cpp' build/windows-debug/compile_commands.json
rg 'raid_session.cpp|extraction_point.cpp|test_raid_session.cpp|test_extraction_point.cpp' build/windows-debug/compile_commands.json
```

若修改类布局后 Debug 测试出现 MSVC `Run-Time Check Failure #2`，或链接器仍引用旧函数签名，先检查 Ninja 是否真实记录头文件依赖：

```powershell
ninja -C build/windows-debug -t deps 'CMakeFiles/Project_Raidline.dir/src/app.cpp.obj'
```

输出应为非零 `#deps` 并列出相关头文件。若是 `#deps 0`，固定代码页后执行 `cmake --fresh --preset windows-debug`，再执行一次 `cmake --build --preset windows-debug --target clean` 和全量构建；旧二进制不可作为验证证据。

## 运行程序与人工检查

Ninja Debug 输出通常位于：

```powershell
& '.\build\windows-debug\Project_Raidline.exe'
```

当前控制：WASD 移动、Space 射击、F 拾取/近距离搜索或打开柜体、Tab 打开玩家背包。程序启动后自动进入 180 秒 Raid；地图左下方半透明绿色区域是撤离点，玩家逻辑中心进入后连续停留 3 秒完成撤离，提前离开会取消并清零。撤离成功后世界冻结，需要重启程序开始新 Raid。Tab 只显示玩家背包；未搜索柜体在范围内显示 `F: SEARCH CABINET`，首次 F 生成一次 Loot 并显示右侧容器，之后提示为 `F: OPEN CABINET`，关闭/取空/重开不会刷新。在双容器界面中，鼠标悬停物品后按 F 或 Ctrl+右键可将整栈按“先合并、后 row-major first-fit”移到另一侧。对弹药按 Ctrl+左键会立即拿起 1 个，按 Shift+左键会立即拿起向上取整的一半，数量虚影随鼠标移动；这两种拿取在只有玩家背包时同样可用，松开到空格/同类未满栈/玩家丢弃区时才提交。Ctrl+Shift+左键无操作。普通背包物品用鼠标按住拖动，拖拽 Pistol/Rifle 时按 R 顺时针旋转 90°；方向键和 Enter 没有库存语义。Esc 优先取消活动拖动，无活动手势时关闭背包。右侧贴边半透明长条是玩家物品丢弃区，成功后所选整栈或数量出现在角色脚下并保留适用的方向、数量和稳定 ID 规则。正式场景在角色下方与右下方放置数量 25 和 40 的两堆已发布 9mm 弹药；未发布视觉资源的逻辑物品不会由正式内容或 LootTable 生成。涉及渲染或输入的任务必须列出要观察的状态，不能只以“程序能启动”作为验收。

## Python 与艺术管线测试

`tests/test_phase1_assets.py` 未注册进 CTest/CI，需要独立执行：

```powershell
poetry run pytest tests/test_phase1_assets.py
```

艺术构建、预览、contact sheet 和 validate 命令会写文件；先按 `$raidline-art-pipeline` 核对范围和权限，不把它们当成纯只读检查。

## Ubuntu / CI 等价路径

GitHub Actions 使用 Ubuntu 22.04：

1. `ci/install_ubuntu_sdl3_deps.sh` 安装 SDL3 系统构建依赖。
2. 将 runner 的 vcpkg fetch/reset/bootstrap 到 `vcpkg.json` 的固定 baseline。
3. 显式安装 `x64-linux` manifest 包。
4. 使用 Ninja、Debug、`VCPKG_MANIFEST_INSTALL=OFF` 和仓库内 `vcpkg_installed` 配置。
5. `cmake --build build` 后运行 `ctest --test-dir build --output-on-failure`。

Windows 2022 job 同样固定 vcpkg baseline，随后 configure、build all 和 full CTest。workflow 的执行策略是：

- 功能分支只由目标为 `main` 的 `pull_request` 事件触发；`push` 只覆盖 `main`，避免同一提交同时产生 push/PR 两套矩阵。
- 同一 PR/分支使用 workflow 级 concurrency；新提交会取消尚未完成的旧运行。
- 一个轻量 Ubuntu job 先比较 PR 与 base 的完整差异。只有 `doc/**` 和 Markdown 的纯文档 PR 会把 Windows/Ubuntu C++ job 标记为 skipped-success；检测失败时默认运行完整矩阵。
- vcpkg installed tree 与仓库工作区内的 `.vcpkg-downloads` 使用按 OS、triplet、baseline 和 `vcpkg.json` 锁定的缓存；缓存未命中仍按固定 baseline 完整安装。不要在 cache path 中使用 runner 注入但未进入 GitHub `env` context 的 `VCPKG_INSTALLATION_ROOT`，否则表达式会退化为错误的 `/downloads` 路径。
- `workflow_dispatch` 保留显式手动运行入口。

Windows runner 使用预装的 vcpkg executable，仅在固定 baseline commit 不存在时执行浅 fetch；manifest 的 `builtin-baseline` 继续固定依赖版本，不再每轮 reset/bootstrap 整套 vcpkg。配置前加载 Visual Studio Developer Shell，并使用 Ninja、Debug、`x64-windows` 与并行 build/CTest，保持与本地 preset 的生成器和构建类型一致。

不要在 CI 通过后再提交“CI 已通过”的状态文档。代码、测试、计划和静态状态文档应在第一次等待 CI 前一次性提交；运行 URL、精确 SHA 和最终结论写入 PR 或 GitHub Issue 评论。这样不会为了记录 CI 结果再次触发 CI。若 CI 暴露真实问题，修复提交仍必须重新运行。

轮询时只等待 PR 的当前 head：优先使用一次 `gh pr checks <number> --watch`，不要同时反复查询每个 job 或历史 run。CI 结束后再做一次最终状态读取即可。

## 故障分类

按第一根因分类，不把级联错误分别修补：

| 层 | 常见证据 | 处理方向 |
| --- | --- | --- |
| 环境/工具链 | 找不到 cl、Ninja、toolchain | Developer Shell、PATH、VCPKG_ROOT |
| 依赖 | baseline 不存在、包构建失败 | vcpkg commit/系统依赖/runner 状态 |
| Configure | generator/cache/toolchain 冲突 | 核对 preset 与 build dir |
| Compile | header、类型重定义、模板诊断 | 找到首个源级错误 |
| Link | unresolved external、重复符号 | target 漏 `.cpp` 或 ODR 问题 |
| Test | assertion/discovery/fixture | 行为契约与 target 注册 |
| Runtime/UI | 资源路径、输入、视觉 | 可复现步骤和人工证据 |
| CI-only | 本地与 runner 差异 | 比较 OS、compiler、generator、triplet、manifest 行为 |

每次汇报准确记录命令、结果、测试数量、CI 对应 commit、跳过项和未执行的人工验收。

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

这个路径只用于本机命令示例，不得写入共享 CMake 配置。若未加载 Developer Shell，已有 Ninja cache 仍可能找到 `cl.exe`，但编译会因缺少 MSVC 标准库 `INCLUDE` 而报 `<optional>`、`<array>` 等文件不存在；此时旧 CTest 二进制不属于新代码的验证证据。

Preset 使用 Ninja、Debug、`x64-windows`，构建目录是 `build/windows-debug`。如果 CMake 报找不到 compiler 或 Ninja，先修复 Developer Shell/PATH；如果 toolchain 变成 `/scripts/buildsystems/vcpkg.cmake`，说明 `VCPKG_ROOT` 为空。

## 定向构建与测试

先构建并运行最小相关目标，再做全量：

```powershell
cmake --build --preset windows-debug --target GridInventoryTest InventoryInteractionTest MouseInventoryInteractionTest
ctest --preset windows-debug -R '^(GridInventoryTest|InventoryInteractionTest|InventoryFrameInputArbitrationTest|MouseInventoryLayoutTest|MouseInventoryInteractionTest|MouseInventoryIntegrationTest|MouseInventoryFrameArbitrationTest)\.'
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

当前 CMake 注册 18 个 GTest executable、295 个 CTest 用例。`MouseInventoryInteractionTest` 编译 canonical `src/inventory_interaction.cpp` 与真实 `GridInventory`，覆盖布局、帧级输入仲裁、鼠标状态和事务集成。可用下列命令证明鼠标测试源真实进入编译数据库：

```powershell
rg 'test_mouse_inventory_interaction.cpp' build/windows-debug/compile_commands.json
```

若修改了 `InventoryInteractionState` 的类布局后 Debug 测试出现 MSVC `Run-Time Check Failure #2`，先对相关 target 做干净重编译；这通常表示增量对象仍按旧类尺寸编译，不应误判成某个 GTest 断言失败。

## 运行程序与人工检查

Ninja Debug 输出通常位于：

```powershell
& '.\build\windows-debug\Project_Raidline.exe'
```

当前控制：WASD 移动、Space 射击、F 拾取、Tab 背包；背包内可用方向键/Enter 移动，也可用鼠标 hover、单击选择和按住拖动。Esc 优先取消活动拖动或键盘放置，无活动会话时关闭背包。涉及渲染或输入的任务必须列出要观察的状态，不能只以“程序能启动”作为验收。

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

Windows 2022 job 同样固定 vcpkg baseline，随后 configure、build all 和 full CTest。workflow 在 `push` 与 `pull_request` 上触发。

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

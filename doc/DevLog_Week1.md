# 📖 《Project_Raidline》开发与学习日志

**阶段：** 第一阶段 - 工程化底座重构与现代化工具链 **记录时间：** Day 1 - Day 2 **核心目标：** 彻底抛弃过时 C++ 编译习惯，建立现代 CMake 构建系统与 Vcpkg 依赖管理底座。

## 📅 Day 1: C++ 现代构建系统重构 (CMake Target-based)

### 🧠 核心新知识点

1. 

   **CMake 的本质：** CMake 不是编译器，而是“元构建系统（Meta-Build System）” 。它负责读取 `CMakeLists.txt` 图纸，然后根据当前操作系统自动生成对应的底层编译文件（如 VS 的 `.sln` 或 Linux 的 `Makefile`） 。

2. **抛弃全局污染，拥抱“基于目标 (Target-based)”：**

   - 旧时代的 CMake 习惯使用 `include_directories()` 等全局指令，容易导致大型项目依赖混乱 。
   - 现代 CMake（3.0+）强制要求把可执行文件或库当成独立的“目标” 。我们通过 `target_link_libraries()` 等指令，仅对指定目标生效，实现了“高内聚，低耦合” 。

3. 

   **跨平台物理隔离墙：** 通过 `set_target_properties(... PROPERTIES CXX_EXTENSIONS OFF)` 关闭编译器扩展，强制代码只使用标准的 C++20 语法，拒绝 GCC 或 MSVC 的方言语法，避免未来移植到 Linux 时发生语法冲突 。

### 🚀 开发进度

- 成功初始化本地 Git 仓库。
- 编写了基础的 C++20 "Hello Zombie" 射击游戏入口代码。
- 成功使用 `cmake -B build` 和 `cmake --build build` 跑通编译流水线 。

## 📅 Day 2: 工业级包管理与 TDD 测试驱动开发 (Vcpkg + GTest)

### 🧠 核心新知识点

1. 

   **Vcpkg 清单模式 (Manifest Mode)：** 通过在根目录编写 `vcpkg.json` 声明第三方库依赖（如 `fmt` 和 `gtest`） 。结合 CMake 的工具链文件（Toolchain），Vcpkg 会在后台自动下载、编译并链接库，彻底告别手动配置环境变量的痛苦 。

2. 

   **TDD 测试驱动开发与黑盒测试：** 编写了游戏核心的 `health_system.h`（玩家生命值扣除逻辑） 。为其编写了边界测试用例（正常扣血、致命伤害归零、负数伤害拦截），由 GTest 自动化防守业务逻辑边界 。

### 👾 经典 Bug 踩坑与填坑记录（重点复盘）

#### ❌ Bug 1：路径中的“隐形杀手”（空格陷阱）

- 

  **报错现象：** `CMake Warning: Ignoring extra path from command line...` 紧接着 CMake 报错找不到 `fmt` 包 。

- 

  **根本原因：** 在命令行指定 Vcpkg 工具链路径时，路径包含了 `VS 2022` 里的空格。终端将空格视作参数分隔符，导致路径被截断，Vcpkg 未能激活 。

- 

  **工程解法：** 处理绝对路径时，**必须使用双引号 `""` 完整包裹路径**。最佳实践是未来将开发环境统一放置在纯英文、无空格的目录下 。

#### ❌ Bug 2：缺乏“时间锁”（Vcpkg Baseline 报错）

- 

  **报错现象：** `error: this vcpkg instance requires a manifest with a specified baseline...` 。

- 

  **根本原因：** 现代工程强调用“可复现构建（Reproducible Builds）”。如果不锁定第三方库的版本，未来官方更新可能导致代码突然崩溃 。

- 

  **工程解法：** 使用 Vcpkg 命令行工具注入 `builtin-baseline`（Git 哈希码），将所有依赖强制死死锁定在当前的历史时间点上 。

#### ❌ Bug 3：CTest 无头苍蝇（找不到可执行文件）

- 

  **报错现象：** `Could not find executable HealthTest_NOT_BUILT` 。

- 

  **根本原因：** Windows 的 Visual Studio 生成器是“多配置”的（生成了 `build/Debug/` 目录），直接运行 `ctest` 时，工具不知道去哪个目录下寻找编译好的 `.exe` 测试文件，从而迷路 。

- 

  **工程解法：** 在 Windows 环境下执行 CTest，必须带上导航仪参数强制指定配置，即运行 `ctest -C Debug` 。

#### ❌ Bug 4：C++ 包含泄露 (Include Bleed) 导致的连环车祸

- 

  **报错现象：** 编译器在 `test_health.cpp` 第5行抛出毫不相干的语法错误 `error C2059: 语法错误:“static_assert”` 。

- 

  **根本原因：** 业务代码 `health_system.h` 里的 `class Player` 结尾**漏写了分号 `;`** 。由于 C++ 的 `#include` 本质是暴力文本复制粘贴，头文件未闭合导致编译器一路读到源文件中的 GTest 宏才彻底宕机，爆出驴唇不对马嘴的错误 。

- 

  **工程解法：** 遇到源文件中出现离奇的宏语法错误时，第一时间排查其 `#include` 的头文件末尾是否丢失了分号 。

**📝 导师批注：** 前两天的“盲写”与终端排错是一场刻意的脱敏训练。你已经掌握了底层工具链的运作逻辑，成功接管了依赖库并打通了自动化测试闭环。下一阶段（Day 3），我们将携带这套坚不可摧的 C++ 底座，前往 Python 端（Poetry）建立企业级环境 。
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



## 📅 Day 3: Python 现代化依赖与虚拟环境管理 (Poetry)

### 🧠 核心新知识点

彻底告别 pip 与 requirements.txt： 引入企业级包管理工具 Poetry。通过 pyproject.toml 统一声明 Python 3.13 运行环境及各种开发依赖，利用生成的 poetry.lock 锁文件，确保了代码在任何机器上的绝对一致性和“确定性重现”，彻底消灭了“依赖地狱”。

交互式与非交互式环境的边界： 深入理解了 poetry shell（沉浸式交互终端）与 poetry run（单指令非交互执行）的区别。在云端 CI 或自动化脚本中，绝不能直接“进入”环境，而是要让机器精准执行指令。

## 🚀 开发进度

成功初始化 Poetry 项目并搭建纯净的 Python 3.13 隔离环境。

编写了 Python 侧的丧尸基础数据结构 (zombie_data.py) 以及配套测试文件框架。

### 👾 经典 Bug 踩坑与填坑记录（重点复盘）

#### ❌ Bug 1：自动化脚本卡死（终端死锁）

报错现象： 尝试在非交互式流水线或自动化脚本中执行 poetry shell 后，进程永远卡住，没有任何后续输出，直至超时崩溃。

根本原因： poetry shell 会拉起一个等待人类敲击键盘输入指令的交互式子终端（tty）。云端虚拟机面前并没有坐着人类，没人会去按回车，导致程序陷入死等。

工程解法： 严格区分本地开发与自动化环境。在自动化脚本中，必须改用 poetry run <command> 命令，相当于告诉机器“把手伸进虚拟环境执行一下，结束后立刻退出来”。

## 📅 Day 4: Python 代码质量门禁与本地拦截器 (Ruff, Mypy, Pytest & Pre-commit)
### 🧠 核心新知识点
严苛的全栈质检阵列： 部署了现代 Python 开发的“三名门神”—— Ruff（基于 Rust 的极速代码格式化与规范检查）、Mypy（静态强类型防御）、Pytest（自动化测试框架）。

本地物理防线建立 (Pre-commit 钩子)： 依靠开发者的自觉是极其不可靠的。通过编写 .pre-commit-config.yaml 引入 Git 钩子，在每次敲下 git commit 动作前强制触发体检。只要代码带病（格式乱、类型错），直接从物理层面死死锁在本地，禁止写入 Git 历史。

### 🚀 开发进度
将 Ruff, Mypy, Pytest 锁入 Poetry 依赖表并完成检查规则的精细化配置。

完成 test_zombie.py 业务测试逻辑验证。

成功激活本地 Git pre-commit 钩子机制。

### 👾 经典 Bug 踩坑与填坑记录（重点复盘）
#### ❌ Bug 1：Mypy 强类型审查误伤测试代码
报错现象： 执行 git commit 时，Mypy 拦截提交并无情报错：Function is missing a return type annotation [no-untyped-def]，矛头直指 test_zombie.py 里的测试函数。

根本原因： 因为在 pyproject.toml 中开启了最严苛的强类型检查（strict = true），Mypy 要求项目中所有函数必须明确声明返回值类型，即使是本来就不返回任何结果的 Pytest 测试函数也不例外。

工程解法： 敬畏强类型契约，为所有测试函数显式补齐 -> None 标注（例如 def test_calculate_threat() -> None:）。

#### ❌ Bug 2：Ruff 自动修复导致的 Git 状态脱节
报错现象： Ruff 检查出格式错误并提示已自动修复（files were modified by this hook），但 Pre-commit 依旧报错退出，导致 git commit 意外失败。

根本原因： Ruff 确实在你的硬盘上帮了忙，把文件格式改对了。但你刚刚试图提交的，是保存在 Git 暂存区（Staging Area） 里的旧版本脏代码。钩子发现硬盘实体与暂存区快照不一致，出于安全机制强行熔断了提交流程。

工程解法： 钩子拦截并修改文件后，必须重新在终端运行 git add .，将“干净”的文件覆盖进暂存区，随后才能顺利执行 git commit。

## 📅 Day 5: 部署云端 CI/CD 双平台监督审查网 (GitHub Actions)
### 🧠 核心新知识点
云端无情监工 (CI/CD 持续集成)： 本地环境再纯净，也可能存在个人的玄学缓存。通过 GitHub Actions，我们让云端在每次代码被 Push 时，自动分配全新、干净的虚拟机，从零跑通拉取、编译、格式检查和测试，彻底消灭“在我的电脑上明明能跑”的经典借口。

Matrix (矩阵) 跨平台策略： 通过在 ci.yml 剧本中配置 strategy: matrix，用同一套指令剧本同时启动 Ubuntu 和 Windows 多台不同系统的机器并行干活，低成本验证 C++ 底层引擎的双端健壮性。

### 🚀 开发进度
编写 .github/workflows/ci.yml YAML 自动化剧本文件。

设计系统隔离的 .gitignore 与 .gitattributes 防线。

成功打通 GitHub Actions 网页端全链路测试，双平台全线点亮绿色 Success 对勾 ✅。

### 👾 经典 Bug 踩坑与填坑记录（重点复盘）
#### ❌ Bug 1：跨平台换行符的历史遗留冲突 (CRLF vs LF)
报错现象： 在 Windows 环境下执行 git add 时，弹出满屏刺眼的系统警告：LF will be replaced by CRLF the next time Git touches it。

根本原因： Windows 默认使用 CRLF（回车+换行）作为行尾标记，而 Linux (Ubuntu 虚拟机) 使用 LF（换行）。代码在两端流转时极易导致假性的代码冲突和格式崩塌。

工程解法： 在项目根目录创建 .gitattributes 文件，写入一行物理契约：* text=auto，把跨平台换行符的动态转换任务强制交由 Git 底层自动接管。

#### ❌ Bug 2：Git 暂存区大污染（垃圾文件误入）
报错现象： 敲击 git status 时，倒吸一口凉气，发现成百上千个 .venv/（虚拟环境依赖）和 __pycache__/ 甚至 .exe 二进制文件被错误放入了暂存区，即将推送到中央仓库。

根本原因： 在 .gitignore 防御规则创建或完全生效之前，就不小心敲下了 git add .。

工程解法： 绝不删库跑路。使用 git reset --soft HEAD~1 执行安全的“时光倒流”，拆开上一次的提交，并编写包含 C++ 产物、Vcpkg 目录和 Python 虚拟环境的工业级 .gitignore 文件，彻底净化 Git 的监控视线。

#### ❌ Bug 3：云端机器的时空错乱与“脏环境”抗命
报错现象： 云端流水线执行 cmake 时崩溃，Vcpkg 报错找不到你本地的 Baseline（历史快照）。为了修复，尝试在 CI 剧本中加入 git pull 更新 Vcpkg，结果又爆出 Your local changes would be overwritten by merge 直接罢工。

根本原因： GitHub 提供的云端系统镜像中，预装的 Vcpkg 版本太旧，不认识“来自未来”的提交节点。并且，镜像打包时带有未清理的本地修改垃圾，导致温柔的 git pull 被 Git 覆盖保护机制无情打断。

工程解法： 抛弃本地的温良恭俭让，直接对云端进行武力覆盖。在 CI 剧本里进入 $VCPKG_INSTALLATION_ROOT 后，打出工业级硬核组合拳：先 git fetch 摸底，再 git reset --hard origin/master 抹除云端机器的一切本地修改，强行将环境重置同步到官方最新主线。

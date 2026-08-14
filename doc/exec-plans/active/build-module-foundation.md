# Build Module Foundation ExecPlan

状态：本地实现与自动化通过，等待提交、PR 和精确 head CI

产品范围来源：`E:\WorkPlace\Projects\C\Project RaidLine GDD\05_Core_Extraction_Alpha_首阶段功能规格.md`

上位计划：`doc/exec-plans/active/core-extraction-alpha.md`

代码基线：`origin/main@5bbddc3`

实施分支：`codex/build-module-foundation`

## 1. 产品结果与完成定义

把现有单一可执行文件和重复测试源码清单迁移为可持续演进的模块化单体构建基础。玩家可见 V0 行为、内容、存档和控制保持不变；后续 ContentRegistry、Profile/AssetRegistry 与 Alpha 切片可以依赖稳定的生产库边界。

完成时必须同时满足：

- 存在 `raidline_domain`、`raidline_simulation`、`raidline_services`、`raidline_sdl_client` 四个生产库目标。
- 每个业务 `.cpp` 只编译进一个生产库；测试只编译测试文件并链接生产库。
- domain 与 simulation 的源文件和公开头不直接包含 SDL。
- 主程序仍由 `Project_Raidline` 目标构建，资源复制路径不变。
- 现有 558 个 CTest 全部保留并通过，Windows 与 Ubuntu 精确 head CI 通过。

## 2. 基线、依赖与排除范围

- PR #55 已以 merge commit `c7a3931` 进入 main，提供 Slice 0 治理与射击窄边界。
- PR #54 已以 merge commit `5bbddc3` 进入 main，提供 RL-INV-003 原子堆叠合同。
- 分支从同时包含两项依赖的 `origin/main@5bbddc3` 创建，起始工作区干净。
- 起始 Windows Debug 构建注册 558 个 CTest；旧 CMake 将同一业务源最多重复列出 14 次。
- Week29 不进入本分支；正式攻击动画、新美术/音频、runtime 资源与 manifest 修改继续暂停。

明确排除：

- 不拆文件、不改类名、不移动 include 路径，不重写 `App` 或 `GameplayWorld`。
- 不加入 ContentRegistry、JSON、ProfileState、AssetRegistry、Persistence 或新玩家功能。
- 不改变 V0 的 3 HP、180 秒 Timeout、只读 Stash、射击、库存、Loot、结算或输入行为。
- 不引入 ECS、服务定位器、事件总线、脚本层、动态插件或安装/导出框架。

## 3. V0 构建差距

| 分类 | 起始状态 | 本 PR 处理 |
| --- | --- | --- |
| 可复用 | 31 个现有业务实现文件、31 个 GTest 可执行目标、`windows-debug` Preset | 保持源码和测试主体不变 |
| 需要重构 | `Project_Raidline` 直接编译全部源码 | 只编译 `main.cpp`，链接 SDL client |
| 需要重构 | 测试重复编译业务源码，最高重复 14 次 | 测试只编译自身文件并链接生产库 |
| 需要新建 | 无稳定模块目标和依赖方向 | 建立 domain → simulation → services → SDL client |
| 需要新建 | 无构建期模块守卫 | 检查生产 `.cpp` 唯一归属及 domain/simulation SDL include |
| 停止扩展 | 继续复制粘贴测试目标源码清单 | 所有 GTest 通过统一 helper 注册 |

## 4. 模块所有权与依赖

```text
Project_Raidline (main.cpp)
└─ raidline_sdl_client (App/Input/Texture/UI interaction)
   └─ raidline_services (GameFlow/GameSession)
      └─ raidline_simulation (GameplayWorld/AI/combat/spatial runtime)
         └─ raidline_domain (items/inventory/raid values/settlement/shot values)
```

- `raidline_domain` 与 `raidline_simulation` 不链接 SDL；配置期同时扫描其归属源文件和公开头的直接 SDL include。
- `raidline_services` 只组合现有流程与模拟；本 PR 不改变长期状态所有权。
- `raidline_sdl_client` 是唯一 SDL/fmt/SDL_image 边界；现有 App 继续原样消费下层对象。
- `ItemInstance`、GridInventory、稳定 ID、命令原子性、RaidSettlement 与 ShotResult 合同均不改变。
- 无保存格式或内容定义变化，也没有迁移、revision 或幂等键影响。

## 5. 实施步骤与退出条件

1. **冻结基线**：核对依赖 PR、分支、HEAD、工作区和 558 个注册测试。退出条件：从 `origin/main@5bbddc3` 建立独立分支。
2. **建立生产库**：把每个业务实现唯一分配到四个库，主程序只链接 SDL client。退出条件：CMake 配置与全目标构建成功。
3. **迁移测试链接**：保留 31 个测试可执行目标和发现名称，移除业务源重复清单。退出条件：注册测试数仍为 558。
4. **加入边界守卫**：配置期拒绝重复生产源和 domain/simulation 直接 SDL include。退出条件：当前目标图通过，Ninja 显示所有 31 个非 main 业务 `.cpp` 各一条生产库编译规则。
5. **回归与交付**：完成 focused、全量 CTest、文档、提交、推送、PR 和 exact-head CI。退出条件：Windows/Ubuntu/范围检查全部成功。

## 6. 验证与证据

自动化门槛：

- `cmake --preset windows-debug` 成功，并执行配置期唯一归属与 SDL 边界检查。
- `cmake --build --preset windows-debug --parallel 8` 成功；本次目标图迁移构建共 68 步。
- Ninja 生产库编译规则为 31 条，覆盖 31/31 个非 `main.cpp` 业务实现，名称无重复。
- 代表 domain/simulation/services/SDL client 与 RL-INV-003 的 focused CTest 133/133 通过。
- 全量 CTest 558/558 通过，0 失败。
- PR 精确 head 的范围检测、Windows C++、Ubuntu C++ CI 必须全部成功。

人工验收：本 PR 不改变可见行为，因此不新增真实窗口门禁，也不由开发代理启动游戏。若代码审查或 CI 暴露运行时链接风险，再把 Slice 0 的既有窗口清单放在所有自动化之后交给用户复验。

证据格式：分支、commit、基线、Preset、目标图/业务源计数、focused/full CTest、CI URL/状态、人工验收是否适用、偏差与未验证风险。

## 7. 提交、PR、风险与回滚

- 一个 focused 分支和一个 PR；CMake 模块迁移与同步状态文档可分为两个 coherent commits。
- 不叠加 Content Registry 或 Profile Asset Registry；后续分支只从本 PR 被接受后的最新 `origin/main` 创建。
- 主要风险是静态库依赖顺序、漏列实现、测试发现名称变化和跨平台 CMake 差异；完整 Windows 构建、558 CTest 与 Ubuntu CI 共同收口。
- 配置期 include 扫描只阻止直接 SDL include，不替代未来更严格的依赖分析；当前源归属与链接图仍以 CMake target 为权威。
- 回滚为完整 revert 本 PR，恢复旧单目标/重复测试源码清单；没有数据、存档或资产迁移需要回滚。

## 8. 进度记录

- [x] 读取范围合同、治理/架构/测试规则和总 ExecPlan。
- [x] fetch 并确认 `origin/main@5bbddc3`、PR #54/#55 已合入、工作区干净。
- [x] 从精确主线创建 `codex/build-module-foundation`。
- [x] 建立四个生产库、统一测试 helper 与配置期边界守卫。
- [x] Windows Debug 配置和 68 步全目标构建成功。
- [x] 31/31 业务 `.cpp` 唯一生产编译规则、558 个注册测试保持不变。
- [x] focused 133/133、全量 558/558 通过。
- [ ] 提交、推送并创建 PR。
- [ ] 精确 head Windows/Ubuntu/范围 CI 全部成功。
- [ ] PR 接受后归档本计划并从最新 main 开始 Content Registry v1。

最后更新：2026-08-14。

# Codex 项目接管、Agent 与 Skill 建设：C++ 教学交接

## 1. 任务名称与状态

- 任务：Project Raidline Codex 全面接管与工程基础建设。
- 日期：2026-08-05。
- 分支：`codex/project-onboarding`，从 Week 17 基线 `a1c164a` 创建。
- 完成度：接管结构、文档、Agent、Skill、Week 17 ExecPlan 和本地验证完成；未实施 Week 17 业务代码，未 push/创建 PR，人工游戏操作未执行。

## 2. 用户可见结果

仓库从“只有历史 DevLog 和美术专用 AGENTS”变为 Codex 可持续接管的工程：拥有精简入口、当前状态、路线、架构、不变量、构建门禁、DoD、活计划、教学台账、5 个自定义 Agent 和 6 个仓库级 Skill。

本轮没有改变玩家运行时行为。Week 17 鼠标背包仍是未接线草稿，但其确定性问题和可执行实施路径已经冻结在活动 ExecPlan 中。

## 3. 修改文件与核心配置

| 文件/目录 | 核心内容 | 作用 |
| --- | --- | --- |
| `AGENTS.md` | 事实优先级、架构护栏、流程导航、艺术红线 | 每次 Codex 会话的稳定入口 |
| `.codex/agents/*.toml` | explorer/implementer/reviewer/verifier/learning-analyst | 项目级专业 Agent |
| `.agents/skills/*/SKILL.md` | 交付、安全、验证、关闭、库存、艺术流程 | 可显式/隐式触发的仓库技能 |
| `.agents/skills/*/agents/openai.yaml` | display/description/default prompt | Skill UI 与调用元数据 |
| `doc/project/` | overview/current state/roadmap | 区分愿景、事实和候选 |
| `doc/architecture/` | ownership/layers/invariants | 核心模型的唯一事实来源 |
| `doc/engineering/` | build/test/CI 与 DoD | 验证和完成门禁 |
| `doc/exec-plans/` | 计划规范与 Week 17 活计划 | 跨阶段工作可恢复执行 |
| `doc/handoffs/` | 模板与本报告 | 中文 C++ 教学交接 |
| `doc/learning/CXX_LEARNING_LEDGER.md` | 已使用知识与学习债 | 跨任务学习连续性 |

没有修改 `src/`、`tests/`、`CMakeLists.txt`、assets 或 manifest。

## 4. 修改前后的执行路径

### 修改前

```text
用户请求
  -> 依赖聊天历史和 Week DevLog
  -> 手工探索源码/CMake
  -> 实现与测试
  -> 没有统一 DoD、活计划或教学关闭路径
```

### 修改后

```text
用户请求
  -> AGENTS.md 判断事实优先级和触发 Skill
  -> explorer/主线程建立行为契约
  -> 必要时 active ExecPlan
  -> 单一 implementer 写入
  -> verifier + reviewer
  -> DoD + task-closeout
  -> completed 中文 C++ 教学交接与学习台账
```

Week 17 的计划执行路径明确为 SDL mouse event → 共享布局转换 → 单一 interaction 状态 → `canMove` 预览 → release request → `tryMove` 一次提交 → 状态 resolve。

## 5. 关键设计决策

1. 根 `AGENTS.md` 保持 61 行左右的导航和高风险红线，详细事实各自放入 `doc/` 或 Skill，避免上下文膨胀。
2. 美术权限边界没有删除：root 保留触发与硬红线，`raidline-art-pipeline` 保存完整角色、版本、manifest、生产、批准和验证流程。
3. 不把 DevLog 中旧的“PR/CI 待完成”当成当前事实；以 Git、源码、CMake 和精确 CI run 为准。
4. 从 `a1c164a` 建立独立 `codex/project-onboarding`，不污染或改写用户的 Week 17 分支。
5. 本轮不修 Week 17 代码；先建立可审计 ExecPlan，避免把项目接管与业务状态机改造混为一个 diff。
6. Week 17 计划选择扩展已接线的 Week 16 单一状态事实源，而不是发布第二套持有 `GridInventory&` 的控制器。

## 6. C++ 语言与标准库

本轮没有修改 C++ 源码，因此没有新增语言特性。审计和计划直接依赖以下已存在知识：

- `enum class`：旧 `class InventoryInteractionState` 与新同名 `enum class` 在全局作用域发生类型重定义。
- `std::optional`：表达无 occupant、无 hover、无 preview、无 origin；不能用 `{0,0}` 伪装“没有值”。
- move-only：`ItemInstance` 禁止复制，拾取/放置必须明确转移唯一所有权。
- 引用成员：新草稿的 `GridInventory&` 不拥有 inventory，生命周期必须长于 controller；计划通过不保存该引用降低耦合。
- `std::vector`：erase/扩容会使引用、迭代器和下标失效，所以交互跨帧只保存 stable ID。
- `[[nodiscard]]`：查询/事务结果必须消费，不能静默丢弃失败。

## 7. 所有权与生命周期

```text
GameplayWorld owns GridInventory
GridInventory::PlacedItem owns ItemInstance
GridInventory::cells_ stores optional stable ID, not ownership
GroundItem owns ItemInstance until a successful pickup transfer
App owns SDL resources and Texture wrappers
```

计划中的 mouse interaction 只保存像素/格子值、手势状态和稳定 ID，不拥有 ItemInstance，也不持有跨帧 PlacedItem 引用。App 在 release 边界调用 `tryMove`，inventory 仍是唯一放置事实来源。

## 8. 数据结构、算法与复杂度

- GridInventory 的 `cells_` 是 row-major 一维 vector，格索引为 `y * width + x`。
- `findFirstFit` 最坏遍历网格和 footprint；当前固定 10×6 规模可接受。
- `canMove` 和计划中的 `originOf` 在当前 `placedItems_` 中线性查找，复杂度 O(n)；保持简单优于提前建立第二索引。
- grab offset 是常数时间向量减法：`clickedCell - actualOrigin`；候选为 `hoveredCell - grabOffset`。
- 拖拽阈值使用距离平方比较，避免每次 motion 求平方根。

## 9. 状态机与事务规则

- Week 16：`Browsing ↔ PlacingItem`；preview 无副作用，只有 `tryMove` 成功才提交。
- Week 17 计划：pointer `Idle → Pressed → Dragging`；低于阈值 release 是 click，不移动。
- keyboard focus 与 mouse hover 独立；同一时刻最多一个 placement 会话。
- outside/invalid/cancel 不调用或不能成功提交 `tryMove`，inventory 全状态保持不变。
- mouse-up 后不能保留 Dragging；失败返回稳定的 selected/idle UI 状态。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终处理 | 验证 |
| --- | --- | --- | --- | --- |
| IDE/编译上下文 | 新 Week 17 header 的 `<optional>` 与 `grid_inventory.h` 出现红线 | 新 `.cpp` 未进入 CMake/compile database，IntelliSense 没有正确 MSVC/include 上下文 | 在当前状态文档和计划中记录；Week 17 阶段 1 必须先建 target | compile database 审计确认仅含旧源 |
| 编译 | 同时包含新旧 interaction header 会触发类型重定义 | 全局 `InventoryInteractionState` 同时是 class 与 enum class | 本轮不掩盖；计划迁移到单一 canonical state 并消除同名文件 | 既有 MSVC 诊断与源码核对 |
| Skill 校验 | `quick_validate.py` 报 `No module named yaml` | bundled Python 缺 PyYAML | 在忽略的 `build/codex-validation` 临时依赖目录安装 PyYAML | 6/6 官方校验通过 |
| Skill 校验 | 中文 Skill 在 Windows 报 GBK decode error | 官方脚本 `read_text()` 使用系统默认编码 | 以 `python -X utf8` 运行同一官方脚本 | 6/6 通过 |
| Codex CLI | `codex --version` 被 Windows 拒绝访问 | Windows Store 应用包 executable 不能从当前 shell 直接启动 | 以官方手册、已安装 app 路径和静态 schema 验证；记录动态 CLI 检查不可用 | Agent TOML/Skill metadata 静态验证通过 |
| Python 测试 | 系统 Python 无 pytest，Poetry 不在 PATH | 当前 shell 缺少项目 dev runner | 在忽略的验证目录安装符合 pyproject 的 pytest | 3/3 pytest 通过 |
| 链接/运行 | 未发生新的链接或游戏运行错误 | 本轮无 C++/runtime 修改 | 无需修复 | 完整 build/CTest 通过；人工运行未执行 |

## 11. 验证证据

- Configure：`cmake --preset windows-debug`，成功；识别 MSVC 14.44、Ninja、固定 vcpkg toolchain/baseline。
- 目标 build：`GridInventoryTest`、`InventoryInteractionTest`，成功。
- 目标测试：库存相关 69/69 通过。
- 完整 build：`cmake --build --preset windows-debug`，成功。
- 全量 CTest：263/263 通过。
- Python：`tests/test_phase1_assets.py`，3/3 通过。
- 配置：6/6 Skill 通过官方 `quick_validate.py`；5/5 Agent TOML 和 6/6 `openai.yaml` 通过解析/字段检查；35 个 Markdown 文件无本地断链。
- 前向测试：用全新只读子任务实际调用 feature-delivery、inventory-domain、art-pipeline；三者均自行识别了单一 canonical 状态、事务边界和批准资产不可覆盖规则，且未修改文件。
- CI：基线 `a1c164a` 的 run 31015344155 成功；本接管分支未 push，因此新增纯文档/配置没有远端 CI。
- 人工验收：未运行游戏；本轮没有玩家行为变更。

## 12. 教学分级

- 已接触、可快速复习：`enum class`、`std::optional`、CMake target、CTest、稳定 ID。
- 可能仍不稳定、应重点讲：move-only 所有权转移、vector 失效、查询/提交分离、失败事务不变、编译 target 与“文件存在”的区别。
- 本次首次出现：Codex Agent/Skill 渐进式上下文、ExecPlan 活文档、证据优先级、compile database 作为接线证据。
- 重复样板：目录建立、YAML display metadata、Markdown 导航，不需要当成 C++ 重点。

## 13. 复盘问题

1. 为什么 `src/inventory/inventory_interaction.cpp` 存在且 CI 绿色，仍不能说 Week 17 已通过测试？
2. `GridInventory&` 引用成员表达什么关系？它为什么不能解决所有权问题？
3. 为什么多格物品的拖拽 origin 不能直接使用 `occupantAt(clickedCell)` 的点击格？
4. `canMove` 与 `tryMove` 分离后，失败事务的测试应该快照哪些状态？
5. keyboard `focusedCell` 与 mouse `hoveredCell` 合并会产生哪些跨输入回归？
6. 为什么 vector 下标不适合作为跨帧物品身份，而 stable ID 适合？
7. 编译错误、链接错误和 test discovery 缺失分别发生在构建链的哪一层？

## 14. 文件与函数定位

- 旧交互：`src/inventory_interaction.h` 的 `InventoryInteractionState`。
- 新草稿：`src/inventory/inventory_interaction.h` 的同名 enum 与 `InventoryInteraction`。
- 网格事务：`src/grid_inventory.cpp` 的 `canMove`、`tryMove`、`canPlaceDefinition`。
- App 编排：`src/app.cpp` 的 `handleInventoryInput`、`confirmInventoryPlacement`、`renderInventoryPlacementPreview`、`renderInventoryOverlay`。
- CMake 证据：`CMakeLists.txt` 的 `Project_Raidline` 和 `InventoryInteractionTest`。
- 下一阶段：`doc/exec-plans/active/week17-mouse-inventory-interaction.md`。

## 15. 技术债与测试债

- 技术债：App 集中度、CMake 重复源码清单、两组同名 interaction 文件、艺术脚本批准资产覆盖保护仅靠流程。
- 测试债：Week 17 无 target/测试；Python 艺术测试不在 CI；无 App 输入/截图端到端测试。
- 配置债：当前 shell 无可直接执行的 Codex CLI/Poetry；Skill validator 对中文 Windows 需要 UTF-8 模式。
- 下一安全任务：按 Week 17 ExecPlan 阶段 1 先增加失败优先测试和 CMake target，再改 canonical 类型。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的任何项目代码。

请根据 doc/handoffs/completed/2026-08-05-codex-project-onboarding.md 和 doc/exec-plans/active/week17-mouse-inventory-interaction.md 进行教学。先解释，再逐步提问；把知识分成“我已接触”“可能不稳定”“首次出现”三类。

重点结合真实项目解释：为什么文件存在但未进 CMake 就不算被构建；class 与 enum class 的全局重名；std::optional 表达无值；move-only ItemInstance 的唯一所有权；stable ID 与 vector 失效；canMove/tryMove 的查询与事务边界；多格物品 grab offset；keyboard focus 与 mouse hover 分离。

不要给我新的项目代码，也不要脱离这次真实 diff 和错误记录写长篇通用教材。每解释一个重点后，用一个贴近 Raidline 的问题检查我是否理解。
```

# Week21 RaidSession 与撤离点 C++ 教学交接

## 1. 任务名称与状态

- 任务：最小 RaidSession、固定撤离点、连续撤离与终局冻结
- 日期/分支/commit：2026-08-07，`codex/week21-raid-session-extraction`，尚未冻结提交
- 完成度：代码、自动测试、安全审查、静态文档与真实窗口 1–8 完成；commit-specific CI 和合入未验证

## 2. 用户可见结果

程序启动后自动进入一局 180 秒 Raid。地图左下方出现代码绘制的半透明绿色撤离区；玩家逻辑中心进入后开始 3 秒连续倒计时，提前离开会清零，重新进入后重新计时。成功后显示 `EXTRACTED` 结果并冻结移动、射击和拾取。超时显示 `RAID ENDED`。

本轮没有结算、Stash、局内重开、正式撤离美术，也没有玩家受伤链路。`PlayerDead` 已是可测试状态，但暂时只能由显式领域命令触发。

## 3. 修改文件与核心符号

| 文件 | 核心符号 | 作用 |
| --- | --- | --- |
| `src/raid_session.h/.cpp` | `RaidSessionState`、`RaidSessionConfig`、`RaidSession` | 六态状态机、两个时钟、终止竞态与死亡命令 |
| `src/extraction_point.h/.cpp` | `ExtractionPoint` | 有限正几何与半开矩形点包含查询 |
| `src/gameplay_world.h/.cpp` | `raidSession_`、`extractionPoint_`、`update` | 所有权、自动开局、移动后撤离判定与终局截断 |
| `src/app.h/.cpp` | `renderExtractionPoint`、`renderDebugText` | SDL 代码绘制、状态/时间/进度和终局反馈 |
| `tests/test_raid_session.cpp` | `RaidSessionTest` | 状态、时间、非法输入、取消和竞态测试 |
| `tests/test_extraction_point.cpp` | `ExtractionPointTest` | 几何、派生溢出与边界测试 |
| `tests/test_gameplay_world.cpp` | `GameplayWorldRaidTest` | 世界集成、真实移动和冻结回归 |
| `CMakeLists.txt` | `RaidSessionTest`、`ExtractionPointTest` | 主程序、集成测试与独立目标接线 |

## 4. 修改前后的执行路径

- 修改前：`SDL input -> App -> GameplayWorld::update -> player/combat/items`，世界没有倒计时或结束条件。
- 修改后：`SDL input -> App -> GameplayWorld::update -> Player::update -> ExtractionPoint::contains(player center) -> RaidSession::update -> 继续玩法或终局 return`。
- 渲染只走 `GameplayWorld const getter -> App -> SDL`；App 不维护副本状态，也不能决定撤离是否成功。
- 库存 overlay 打开时玩法输入仍被屏蔽，但世界继续更新空输入，因此 Raid 时钟与玩家原地撤离继续；终局后 App 关闭 overlay。

## 5. 关键设计决策

1. 使用独立 RaidSession，而不是在 App 中放几个 bool 和 float。这样 SDL、渲染与规则分离，全部边界可在 GTest 中验证。
2. 撤离判定使用玩家逻辑中心。仅 AABB 擦边不会撤离，渲染 sprite 的透明区也不会影响玩法。
3. Rect 使用左/上包含、右/下排除。相邻区域共享边界时，一个点不会同时属于两边。
4. 大 deltaTime 不是固定“先查撤离再查超时”，而是比较两个事件还剩多久；先发生者获胜，完全同时超时优先。
5. 终局形成的同一帧立刻 return，避免撤离成功后同帧继续拾取、射击、命中或计分。
6. 不把 Enemy Health 冒充为玩家生命。PlayerDead 接口保留真实未来接线点，当前产品边界写明未接入。

## 6. C++ 语言与标准库

- 语言特性：`enum class` 六态、聚合配置、委托构造、默认成员初始化、`switch` 全状态映射、提前返回。
- 标准库组件：`std::isfinite`、`std::min`、`std::clamp`、`std::invalid_argument`、`std::logic_error`。
- `const`、引用、值、指针与 move：ExtractionPoint 的 `bounds()` 和 GameplayWorld 的两个 getter 返回 `const&`，避免复制并禁止外部 mutation；点和配置是小值类型；没有新增裸资源或所有权转移。
- `noexcept` / `[[nodiscard]]`：无分配的状态查询与命令标记 `noexcept`；`start()`、`markPlayerDead()` 和关键查询标记 `[[nodiscard]]`，调用者必须面对成功/失败。

## 7. 所有权与生命周期

- GameplayWorld 独占一个 ExtractionPoint 和一个 RaidSession；它们与世界同生共死。
- App 只借用同一帧内的 const 引用做渲染，没有跨帧保存指针/引用。
- 新类型不拥有 SDL_Texture、ItemInstance 或 vector 元素；没有引入移动后悬空、迭代器失效或双重释放风险。
- RaidSession 构造先验证配置，GameplayWorld 完成其他成员构造和地面生成后才 start，避免半构造世界对外表现为已开局。

## 8. 数据结构、算法与复杂度

- RaidSession 只保存两个 float 和一个 enum，空间复杂度 O(1)。
- 每次 update 做固定数量的比较、加减与 clamp，时间复杂度 O(1)。
- ExtractionPoint::contains 是四次边界比较，O(1)。
- GameplayWorld 现有敌人/投射物/vector 路径不因本轮增加额外遍历；终局反而在入口直接 O(1) return。

## 9. 状态机与事务规则

- `Preparing --start--> InRaid`。
- `InRaid --inside--> Extracting --outside--> InRaid`，outside 会把进度清零。
- `Extracting --3 seconds first--> Extracted`。
- `InRaid/Extracting --raid timeout first or exact tie--> RaidEnded`。
- `InRaid/Extracting --markPlayerDead--> PlayerDead`。
- 三个终局不可逆；后续 start/update/death 都是失败或 no-op。
- 非正/非有限 deltaTime 不推进时钟，但 inside/outside 观察仍能转换或取消 Extracting。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终修复 | 验证 |
| --- | --- | --- | --- | --- |
| 环境 | 普通 PowerShell 无法识别 `ctest` | VS CMake bin 未进入该 shell 的 PATH | 使用与配置相同的 VS `ctest.exe` 绝对路径 | 后续聚焦与全量测试成功 |
| 安全审查 | 位置和尺寸各自有限仍可能产生无限 right/bottom | 两个巨大 float 相加可溢出 | 构造时同时验证派生右/下边界有限 | 新增溢出构造测试通过 |
| 编译 | 未发生源级编译错误 | — | — | 全目标构建通过 |
| 运行 | 未复现 `gtest_ar_` 栈损坏 | 依赖追踪修复保持有效 | 直接运行三个测试程序 | 90/90 通过 |

## 11. 验证证据

- Configure：`cmake --preset windows-debug`，通过。
- Build：受影响目标及 Windows Debug 全目标构建通过；仅出现两个既有 MouseInventory 测试忽略 `[[nodiscard]]` 的 C4834 警告，本轮没有新增 warning。
- 目标测试：Raid/Extraction/GameplayWorld 聚焦 CTest 90/90；独立领域最终 23/23。
- 测试程序直跑：RaidSessionTest 18/18、ExtractionPointTest 5/5、GameplayWorldTest 67/67，共 90/90。
- 全量 CTest：416/416；`ctest -N` 注册 416。
- 接线：compile database 证明两个业务源进入主程序和 GameplayWorldTest，测试源进入独立 target。
- 其他测试：未修改任何艺术资源，`tests/test_phase1_assets.py` 不适用。
- CI：尚未创建冻结提交/PR，未验证。
- 人工验收：用户在真实窗口确认 1–8 全部通过。

## 12. 教学分级

- 用户已接触、可快速复习：`enum class`、组合所有权、const getter、有限浮点检查、状态机与 CMake target。
- 可能仍不稳定、应重点讲：终局 sticky、不合法状态转换、同帧 mutation 截断、逻辑几何与渲染几何分离。
- 本次首次出现：同一个 deltaTime 内两个 deadline 的先后比较；精确 tie 的产品规则；派生浮点边界溢出。
- 重复样板、无需展开：GTest target 的 compile features/include/link 固定结构、SDL 基础绘制调用。

## 13. 复盘问题

1. 为什么 `if (raid expired) ... else if (extract complete)` 的固定顺序不能正确处理所有大 deltaTime？
2. 为什么撤离完成时只从 Raid 剩余时间扣除“到撤离事件的时间”，而不是整个 deltaTime？
3. 为什么玩家 sprite 比 32×32 逻辑体大时，撤离仍应读取逻辑中心？
4. `Extracted` 为什么必须是 sticky，而不能在下一帧回到 RaidEnded？
5. 终局形成后的提前 return 防止了哪些同帧副作用？
6. 为什么配置字段有限还不够，ExtractionPoint 仍要检查 `position + size`？
7. 如果 Week23 接入玩家 Health，哪个层应该调用 `markPlayerDead()`，哪个层不应该？

## 14. 文件与函数定位

- `src/raid_session.cpp`：构造验证、`start`、`update`、`markPlayerDead`、状态名。
- `src/extraction_point.cpp`：派生边界验证与 `contains`。
- `src/gameplay_world.cpp`：构造末尾自动 start；`update` 的移动后撤离判定和终局 return。
- `src/app.cpp`：`App::update` 终局 overlay 关闭、`renderExtractionPoint`、`renderDebugText`。
- `tests/test_raid_session.cpp`、`tests/test_extraction_point.cpp`、`tests/test_gameplay_world.cpp`：领域和集成证据。

## 15. 技术债与测试债

- 技术债：PlayerDead 没有玩家 Health/受击来源；没有结算、Stash、局内重开；App 继续集中渲染职责；时间步仍是可变 deltaTime。
- 测试债：没有 App 级原生 SDL 事件/截图自动化；超时的视觉结果只由领域测试覆盖；Ubuntu 仅能在 PR CI 验证。
- 下一安全任务：冻结两条提交并完成单一 PR/CI；合入后进入 Week22 结算与最小 Stash。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 doc/handoffs/completed/2026-08-07-week21-raid-session-extraction.md 和对应真实 diff 进行教学。先解释，再逐步提问；把知识分成“我已接触”“可能不稳定”“首次出现”三类。重点结合 RaidSession::update 中两个 deadline 的竞争、sticky 终局、ExtractionPoint 半开边界、GameplayWorld 同帧提前 return 与 App 只读渲染，避免脱离项目的大段教材式扩展。
```

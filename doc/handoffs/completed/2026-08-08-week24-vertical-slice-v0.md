# Week24 垂直切片 V0 收口：C++ 教学交接

## 1. 任务名称与状态

- 任务：玩家生命、敌人接触死亡与成功/失败垂直切片回归
- 日期/分支/commit：2026-08-08，`codex/week24-vertical-slice`，feature commit `d8c58e6`，PR #44，merge commit `1415238`
- 完成度：代码、安全审查、本地自动测试、真实窗口 1–8、精确 head Windows/Ubuntu CI、PR 与合入全部完成

## 2. 用户可见结果

玩家现在拥有 3 HP，左上角显示 `Player HP: current/max`。活动 Raid 中，玩家与存活敌人的逻辑 AABB 正面积重叠会立即受到 1 点伤害，随后进入 0.75 秒接触伤害冷却；生命降到 0 时同一帧进入 `PlayerDead`，携带物按既有结算规则显示 `LOST` 并被清空。按 `N` 开始下一 Raid 后，玩家恢复 3/3 HP、背包为空，世界重置而 Stash 保留。

本轮不包含敌人寻路/攻击动画、远程攻击、治疗、击退、无敌闪烁、音效、美术资源、Stash 配装、持久化或库存 #38/#39。

## 3. 修改文件与核心符号

| 文件 | 核心符号 | 作用 |
| --- | --- | --- |
| `src/player.h/.cpp` | `Player::takeDamage`、`health`、`maxHealth`、`isDead`、`Health health_` | Player 唯一拥有生命状态 |
| `src/gameplay_world.h/.cpp` | `damagePlayer`、`markPlayerDead`、`resolveEnemyContactDamage`、`playerContactDamageCooldownRemaining_` | 接触伤害、死亡终局与致死帧截断 |
| `src/app.cpp` | `App::renderDebugText` | 只读显示 Player HP 并下移既有调试行 |
| `tests/test_player.cpp` | 3 个 Player Health 测试 | 初始、非致命、致命与重复死亡 |
| `tests/test_gameplay_world.cpp` | Player Health、领域伤害、接触冷却/致死测试 | 世界级状态与帧顺序回归 |
| `tests/test_game_session.cpp` | 成功切片测试、第二局死亡损失扩展 | 串联搜索/转移/撤离/死亡/结算/重开 |
| `doc/exec-plans/completed/week24-vertical-slice-v0.md` | Week24 已完成计划 | 冻结范围、证据与人工清单 |

## 4. 修改前后的执行路径

- 修改前：敌人与玩家可以重叠但没有结果；只有测试直接调用 `GameplayWorld::markPlayerDead()` 才能触发死亡结算，Player 本身没有生命状态。
- 修改后：`GameplayWorld::update` 先移动 Player、更新撤离状态和 Enemy，再缩短接触冷却并查询 AABB；命中时调用 `damagePlayer(1)`。非致命伤害继续本帧，致死伤害同步把 RaidSession 转为 PlayerDead 并在射击/投射物/命中前返回。GameSession 随后调用 RaidSettlement 清空携带物并进入 BetweenRaids。
- 输入、状态、查询、提交和渲染：SDL/WASD 仍只形成 GameplayInput；碰撞查询读取 const Player/Enemy bounds；伤害命令提交 Player Health 与 RaidSession；结算提交背包损失；App 只通过 const getter 显示 HP 和终局。

## 5. 关键设计决策

- Player 唯一拥有 Health，拒绝在 GameplayWorld、GameSession 或 App 保存整数 HP 副本。
- 接触伤害使用 0.75 秒 cooldown 与“一次 update 最多一击”，避免伤害速度随渲染帧率和重叠敌人数变化。
- 在 Enemy 移动后检查最终位置，但在玩家射击和投射物命中前结算；接触致死优先截断本帧后续战斗 mutation。
- 保留 `markPlayerDead()` 兼容入口，但让它复用 `damagePlayer(currentHealth)`，不能绕开 Player Health。
- 没有为测试开放可变 Player 或位置 setter；GameSession 成功测试使用真实移动、搜索、转移、撤离与重开路径。

## 6. C++ 语言与标准库

- 语言特性：对象组合、类不变量、早返回、`[[nodiscard]]` 命令结果、匿名命名空间常量与 helper。
- 标准库组件：`std::any_of` 查询任一存活敌人碰撞，`std::max` 把 cooldown clamp 到 0，测试随机源使用 `std::vector<std::uint32_t>`。
- `const`、引用、值、指针与 move 语义：App 保存当前帧 `const Player&` 只读显示；碰撞 lambda 接受 `const Enemy&`；不保存跨 vector mutation 的 Enemy 引用；物品转移仍保持 move-only 事务。
- `noexcept` / `[[nodiscard]]`：生命 getter 为 `noexcept`；伤害命令标记 `[[nodiscard]]`。`damagePlayer` 不声明 `noexcept`，因为非法非正伤害沿用 Health 的异常契约；`markPlayerDead()` 以活动且存活的正生命作为内部前置条件，调用致命伤害路径。

## 7. 所有权与生命周期

Player 直接组合拥有 `Health health_{3}`，生命周期与当前 GameplayWorld 中的 Player 完全一致。GameplayWorld 拥有 Player、Enemy、RaidSession 与 contact cooldown；GameSession 重建世界时，这些单局状态一起销毁并由新世界恢复默认值。Stash 仍由 GameSession 跨局拥有，不受 Player 销毁影响。

本轮没有新增裸指针、动态资源或所有权转移。Enemy 遍历只在 `std::any_of` 调用期间读取，未跨后续 projectile hit 的 vector mutation 保存引用。测试在柜体转移前把引用中的 ID/quantity 复制为值，转移后不再使用该引用。

## 8. 数据结构、算法与复杂度

- 数据结构：Player 内一个固定尺寸 Health；GameplayWorld 内一个 float cooldown；Enemy 继续保存在 vector。
- 算法步骤：cooldown clamp → `std::any_of` 扫描存活敌人 → AABB 查询 → 至多一次伤害提交 → 致死早返回。
- 时间复杂度：接触查询为 O(E)，E 是敌人数；额外空间 O(1)。当前正式世界只有一个 Enemy。
- 当前规模下可接受但需记录的线性路径：未来敌人数显著增加时可考虑空间索引，但不能为当前单敌人原型提前引入。

## 9. 状态机与事务规则

- 状态和转换：Player HP 3→2→1→0；首次到 0 同步把 InRaid/Extracting 转为 sticky PlayerDead；GameSession 在结算后转 BetweenRaids。
- 查询与提交：AABB 和 cooldown 条件是查询；`Player::takeDamage` 与 `RaidSession::markPlayerDead` 是同一世界命令中的提交步骤。
- 成功后状态：非致命时 HP 减 1 且 cooldown=0.75；致命时 HP=0、Raid=PlayerDead、本帧停止后续战斗；下一 GameSession update 完成损失结算。
- 失败后必须不变：非活动 Raid 的 `damagePlayer` 返回 false，不改 HP/Raid；cooldown 未结束或无碰撞时不改 HP。
- 取消/边界/外部事件：AABB 仅边缘接触不伤害；非正 deltaTime 不缩短 cooldown；撤离/超时若先在 RaidSession update 形成终局，则本帧不再移动 Enemy 或处理接触。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终修复 | 验证 |
| --- | --- | --- | --- | --- |
| 设计/状态 | 旧 `markPlayerDead()` 可产生 PlayerDead 但 Player 仍为 3 HP | 新增 Health 后旧命令仍直接修改 RaidSession，形成双事实源 | 兼容命令改走 `damagePlayer(currentHealth)` | 聚焦测试两轮 103/103；死亡命令断言 HP=0 |
| 环境 | 普通 PowerShell 报找不到 `ctest` | `ctest.exe` 未在普通 shell PATH | 改用 Visual Studio CMake 的完整路径 | 聚焦与全量 CTest 通过 |
| 沙箱 | 首次只读证据组合中的 `ctest -N` 无法写 LastTest.log | 构建目录写权限未提升 | 以构建权限单独重跑注册统计 | `Total Tests: 453` |
| 编译/链接/运行 | 未发生代码层失败 | 首轮实现直接通过受影响目标编译 | 无需修复 | 主程序及三个测试目标构建成功 |

## 11. 验证证据

- Configure：UTF-8 Developer PowerShell 中 `cmake --preset windows-debug` 成功。
- Build：受影响的 `PlayerTest GameplayWorldTest GameSessionTest Project_Raidline` 成功；修复后重新构建世界/会话/主程序成功；最终 `cmake --build --preset windows-debug` 全目标成功，无新增警告。
- 目标测试：首轮聚焦 CTest 103/103；一致性修复后再次 103/103。
- 全量 CTest：453/453。
- 其他测试：Player 新测试直接 3/3、GameplayWorld 生命/接触直接 4/4、GameSessionTest 直接 8/8；无 `gtest_ar_`/运行库错误。`ctest -N` 注册 453；compile database 与 Ninja `#deps 197/168` 证明接线有效。
- Phase1 pytest：未执行；本轮无艺术资源变化。
- CI：feature commit `d8c58e6` 的 GitHub Actions run 31244714973 全部通过；范围检测 7 秒、Ubuntu 1 分 9 秒、Windows 3 分 12 秒。
- 人工验收：用户在 Windows Debug 真实窗口中确认 1–8 全部通过。

## 12. 教学分级

- 用户已接触、可快速复习：Health 组合、AABB、cooldown、sticky 终局、同帧早返回、GameSession/RaidSettlement。
- 可能仍不稳定、应重点讲：唯一事实源、`noexcept` 的真实执行路径、接触伤害与终局的原子关系、类布局变化后的依赖重编译。
- 本次首次出现：Player 生命接线、真实玩法到 PlayerDead 的路径、成功/失败双垂直切片集成测试。
- 重复样板、无需展开：GTest 基础宏、SDL DebugText、CMake target 常规属性。

## 13. 复盘问题

1. 为什么不能让 GameplayWorld 保存 `int playerHp_`，同时让 Player 也拥有 Health？
2. 为什么接触致死必须在投射物生成与命中处理之前早返回？
3. `damagePlayer` 对非致命伤害返回 false 表达的是什么，调用方还应观察哪个状态？
4. 为什么 `markPlayerDead()` 不能继续直接调用 RaidSession？
5. cooldown 为什么是 GameplayWorld 单局状态，而不是 Player 或 GameSession 的跨局状态？
6. GameSession 成功测试如何证明搜索、转移、结算和重开并非各自孤立地绿色？
7. 为什么测试在柜体转移前复制 ID/quantity，转移后不继续使用 `PlacedItem&`？

## 14. 文件与函数定位

- `src/player.h/.cpp`：Player Health 所有权与伤害接口。
- `src/gameplay_world.cpp`：`update`、`damagePlayer`、`markPlayerDead`、`resolveEnemyContactDamage`。
- `src/app.cpp`：`App::renderDebugText` 的 Player HP 展示。
- `tests/test_gameplay_world.cpp`：接触路径、cooldown 与致死帧顺序。
- `tests/test_game_session.cpp`：成功/失败跨系统回归。
- `doc/exec-plans/completed/week24-vertical-slice-v0.md`：冻结合同、验证与人工清单。

## 15. 技术债与测试债

- 技术债：接触伤害使用离散帧末 AABB，极大 deltaTime 可能穿越而不命中；伤害数值为 V0 常量；App 调试文本仍为开发 UI；没有复杂敌人攻击、治疗或受伤表现。
- 测试债：缺少 App 级自动输入/截图测试；本轮真实碰撞手感、文字布局与完整窗口流程已人工通过，Ubuntu/Windows CI 也已通过。
- 下一安全任务：按 Week25 计划建立 `MainMenu → Base → Raid → RaidResult → Base` 顶层流程壳；后续依次推进鼠标射击、敌人攻击动作、敌人 AI 和战斗手感收口，库存 #38/#39/#28 继续作为独立稳定化任务。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 `doc/handoffs/completed/2026-08-08-week24-vertical-slice-v0.md` 第 2–15 节和对应真实 diff 进行教学。先解释，再逐步提问；把知识分成“我已接触”“可能不稳定”“首次出现”三类。重点结合 Player 唯一拥有 Health、GameplayWorld 接触 cooldown、damagePlayer/markPlayerDead 的终局一致性、致死帧早返回、GameSession 双路径回归和 Ninja 类布局依赖证据，避免脱离项目的大段教材式扩展。
```

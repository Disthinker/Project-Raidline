# Week25 顶层游戏流程壳 C++ 教学交接

## 1. 任务名称与状态

- 任务：MainMenu、Base、Raid、RaidResult 单地图流程壳。
- 日期/分支/commit：2026-08-08，`codex/week25-game-flow`，feature commit `23cd19b`，PR #46，merge commit `08e4475`。
- 完成度：Completed；实现、安全审查、本地自动验证、真实窗口 1–10、精确 head CI 与合入全部完成。

## 2. 用户可见结果

程序不再启动即进入 Raid。玩家从代码绘制的主菜单进入基地，在基地查看只读 Stash 并部署；Raid 完整结算后先看结果，再返回基地并部署下一局。MainMenu、Base 与 RaidResult 不推进世界。旧 `N` 重开移除，非 Raid 屏幕使用 Enter、数字键盘 Enter 或鼠标主按钮确认。

本轮没有最终 UI 美术、多地图选择、配装、保存、鼠标瞄准、敌人攻击或 AI。

## 3. 修改文件与核心符号

| 文件 | 核心符号 | 作用 |
| --- | --- | --- |
| `src/game_flow.h/.cpp` | `GameFlowState`、`GameFlow` | SDL 无关四态流程、唯一拥有 GameSession、受控转换与更新路由 |
| `src/app.h/.cpp` | `handleScreenConfirm`、四个 render 分支 | SDL 事件翻译、转换帧消费、代码绘制占位界面 |
| `src/input_system.h/.cpp` | `GameAction::ScreenConfirm` | Enter 边沿确认，移除 N 映射 |
| `tests/test_game_flow.cpp` | 8 个 `GameFlowTest` | 合法/非法转换、冻结、撤离/死亡/超时、两局、Blocked、Stash/ID 契约 |
| `tests/test_input_system.cpp` | ScreenConfirm 回归 | 主 Enter、数字键盘 Enter与 N 取消映射 |
| `CMakeLists.txt` | `GameFlowTest` | 主程序/专用测试接线与 MSVC UTF-8 |

## 4. 修改前后的执行路径

- 修改前：SDL event → App → GameSession/GameplayWorld；程序直接在 Raid，完整结算按 N 调用 `startNextRaid()`。
- 修改后：SDL event → App 屏幕命令或 GameplayInput → GameFlow → 仅 Raid 转发到 GameSession → GameplayWorld。
- 渲染由 `GameFlowState` 选择唯一屏幕；转换成功帧立即返回，确认输入不会落入新屏幕或世界。

## 5. 关键设计决策

- 采用小型 `GameFlow`，不引入通用 SceneManager、ECS 或事件总线。
- 保留 GameSession 对 Stash、GameplayWorld、RaidSettlement 的既有所有权；GameFlow 只在外层再包一层组合根。
- 第一次部署激活构造时已准备但被冻结的 Raid 1，避免本轮扩大到延迟世界工厂；后续部署仍使用强失败语义的 `startNextRaid()`。
- App 暂时保留 `GameSession&` 非拥有别名以控制机械改动；成员声明顺序和删除复制/移动防止错误重绑定。

## 6. C++ 语言与标准库

- 语言特性：`enum class`、组合、引用成员、删除特殊成员、switch 状态路由。
- 标准库组件：`std::char_traits<char>::length` 用于占位按钮文字居中；既有值类型输入继续复用。
- `const`、引用、值、指针与 move 语义：GameFlow 提供 const/非 const GameSession 引用；引用不拥有、不延长生命周期；本轮不移动 Stash 或 ItemInstance。
- `noexcept` / `[[nodiscard]]`：无抛出状态查询/转换标注 `noexcept`，可能被忽略的命令结果标注 `[[nodiscard]]`。

## 7. 所有权与生命周期

App 唯一拥有 GameFlow，GameFlow 唯一拥有 GameSession，GameSession 继续唯一拥有长期 Stash、当前 GameplayWorld 和当前 RaidSettlement。`gameFlow_` 在 `gameSession_` 别名前构造、后析构；App 禁止复制和移动。屏幕状态不复制 Stash、世界、Raid 编号或稳定 ID。

## 8. 数据结构、算法与复杂度

- 数据结构：一个四值枚举、一个拥有 GameSession 的组合对象、一个首次部署布尔标志。
- 算法：命令先检查当前状态和会话条件，再做单次状态转换；update 仅在 Raid 转发。
- 时间/空间复杂度：流程查询与转换为 O(1)，额外空间 O(1)；实际新 Raid 构造成本继续由 GameSession/GameplayWorld 决定。

## 9. 状态机与事务规则

- 状态：`MainMenu → Base → Raid → RaidResult → Base`。
- 查询与提交：`state/isRaidScreen` 无副作用；`startGame/deploy/returnToBase` 是显式命令。
- 成功后状态：一次输入边沿只前进一个合法状态；后续部署 Raid 编号只增加 1。
- 失败后必须不变：错误状态、活动局、Pending 或 Blocked 不改变流程、会话、Stash、世界或编号。
- 外部事件：只有完整 `BetweenRaids` 进入 RaidResult；Blocked 保持 Raid 且不能绕过。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终修复 | 验证 |
| --- | --- | --- | --- | --- |
| 环境 | CMake 在 `C:\Users\25113\source\repos` 查找 preset | Developer PowerShell 自动切换目录 | 加载环境后显式 `Set-Location` 回仓库 | 聚焦与全量构建成功 |
| 环境 | 普通 PowerShell 找不到 `ctest`，沙箱首次不能写 LastTest 日志 | 工具未在 PATH 且 E 盘构建日志需授权 | 使用 VS 自带 `ctest.exe` 并以批准权限运行 | `ctest -N` 显示 462 |
| 编译/链接/运行/测试 | 未发生代码失败或 Runtime Library 错误 | 不适用 | 不适用 | 全量 462/462，专用程序 3/3、8/8 |

## 11. 验证证据

- Configure：Windows Debug preset 既有配置成功。
- Build：聚焦三目标和全部 target 成功。
- 目标测试：InputSystemTest + GameFlowTest 31/31；程序直接运行 3/3 与 8/8。
- 全量 CTest：462/462；注册数量 462。
- 其他测试：compile database 包含主程序/专用测试的新源；`app.cpp.obj` 为 `#deps 198` 且依赖 `game_flow.h`。
- CI：[Actions run 31247705924](https://github.com/Disthinker/Project-Raidline/actions/runs/31247705924) 对精确 head `23cd19b` 全部通过：范围检测 5 秒、Ubuntu 1 分 10 秒、Windows 3 分 31 秒。
- 人工验收：用户于 2026-08-08 确认真实窗口 1–10 全部通过。

## 12. 教学分级

- 用户已接触、可快速复习：enum 状态机、just-pressed、组合所有权、查询/命令分离。
- 可能仍不稳定、应重点讲：引用成员生命周期、成员初始化/销毁顺序、输入消费与跨屏泄漏。
- 本次首次出现：顶层流程组合根、非活动子系统冻结、删除 App 复制/移动以保护别名。
- 重复样板、无需展开：CMake 测试目标源清单、SDL DebugText 占位绘制。

## 13. 复盘问题

1. 为什么只隐藏 Raid 渲染不能保证基地里世界冻结？
2. 为什么 GameFlow 而不是 App 应成为 GameSession 的唯一所有者？
3. `GameSession& gameSession_` 为什么不拥有对象，什么情况下会悬空？
4. 为什么第一次 Deploy 不调用 `startNextRaid()`？
5. 为什么转换成功帧必须立即 return？
6. Blocked 为什么不能映射为 RaidResult？

## 14. 文件与函数定位

- `src/game_flow.h/.cpp`：顶层状态、转换和唯一所有权。
- `src/app.cpp`：`processEvents`、`update`、`render` 与四个屏幕绘制方法。
- `src/input_system.cpp`：`mapScancodeToAction`。
- `tests/test_game_flow.cpp`：跨两局与冻结回归。

## 15. 技术债与测试债

- 技术债：App 仍集中大量 SDL/库存职责；非拥有 GameSession 别名是受保护的过渡接线；测试 target 继续重复编译业务源码。
- 测试债：没有 App 级窗口自动化，屏幕点击范围、文本和输入泄漏仍需真实窗口验收。
- 下一安全任务：按活动 ExecPlan 开始 Week26 鼠标瞄准、射击与 V0 后坐力，不夹带弹匣/换弹、敌人攻击或 AI。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 doc/handoffs/completed/2026-08-08-week25-game-flow-shell.md 这次任务的真实 diff、执行路径、测试与错误记录进行教学。先解释，再逐步提问；把知识分成“我已接触”“可能不稳定”“首次出现”三类，重点结合 GameFlow、GameSession、App 的所有权、引用生命周期、状态机与输入消费，避免脱离项目的大段教材式扩展。
```

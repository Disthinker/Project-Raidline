# Week26 鼠标瞄准、射击与 V0 后坐力 C++ 教学交接

## 1. 任务名称与状态

- 任务：Week26 鼠标瞄准、左键连续射击、确定性扩散、V0 可视后坐力、高速火光弹体与命中反馈。
- 日期/分支/commit：2026-08-08；`codex/week26-mouse-aim-shooting-recoil`；基线 `main@ff828de`，本报告随待创建的 Week26 feature commit 一并冻结。
- 完成度：代码、文档、Windows Debug 全目标构建、聚焦 CTest 57/57、全量 CTest 483/483 已完成；用户已确认人工验收 1–10、11–13，最终手感验收 15 与精确 feature head Windows/Ubuntu CI 尚未验证。

## 2. 用户可见结果

- 鼠标位置独立于 WASD 决定角色瞄准方向；玩家可以向一个方向移动、向另一个方向射击。
- 活动 Raid 且库存关闭时，左键与保留的 Space 回归入口共同驱动同一个射击领域状态；菜单、库存、切屏、失焦与终局不会泄漏左键射击。
- 第一发精确，持续射击按 0.12 秒 cadence 增加最多 6° 的确定性扩散；准星用外扩表现扩散与 V0 后坐力，停火后恢复。
- 系统鼠标在战斗中隐藏，只显示代码准星；进入 UI、终局或退出时恢复。
- 投射物从最终瞄准方向的玩家外缘生成，使用方向无关 8×8 AABB、1200 px/s 速度、细小火光弹头与两段拖尾。
- 高速弹丸用有限子步推进，0.30 秒大帧间隔仍可命中；命中产生 16 枚短寿命白热、金黄、红橙火花。
- 本轮不包含弹药消耗、弹匣/换弹、武器装备、相机震动、音效、敌人攻击或 AI。

## 3. 修改文件与核心符号

| 文件 | 核心符号 | 作用 |
| --- | --- | --- |
| `src/weapon_fire.h/.cpp` | `WeaponFireConfig`、`ShotSpec`、`WeaponFireState` | SDL 无关的 cadence、扩散、后坐力、恢复与确定性序列。 |
| `src/gameplay_input.h` | `aimWorldPosition` | 把可选世界瞄准点作为值快照送入领域层。 |
| `src/input_system.h/.cpp` | primary pointer held/edge/suppression | 区分物理按住、玩法按住、单帧边沿与 UI 抑制。 |
| `src/player.h/.cpp` | `Player::faceDirection` | 校验并归一化显式瞄准方向，非法输入保持旧朝向。 |
| `src/gameplay_world.h/.cpp` | `weaponFire_`、方向枪口、projectile substeps | 唯一拥有射击状态，创建弹丸并可靠解析高速命中。 |
| `src/projectile.h/.cpp` | `Projectile::velocity` | 向渲染层提供只读速度值，不暴露可变状态。 |
| `src/particle_system.h` | `ParticleBurstConfig` 默认值 | 命中反馈改为更快、更短、更紧凑的 16 粒子 burst。 |
| `src/app.h/.cpp` | `makeGameplayInput`、`renderAimCrosshair`、`renderProjectiles`、`renderParticles`、`syncSystemCursorVisibility` | SDL 坐标/光标适配与代码绘制表现。 |
| `tests/test_weapon_fire.cpp` | `WeaponFireStateTest` | 独立覆盖射击状态合同。 |
| `tests/test_input_system.cpp` | pointer fire/suppression tests | 覆盖 held、edge、释放、UI 抑制、失焦和 Space 隔离。 |
| `tests/test_gameplay_world.cpp` | aim、muzzle、speed、substep、impact tests | 覆盖领域集成与高速防穿透。 |
| `tests/test_game_session.cpp` | `NextRaidStartsWithFreshWeaponFireState` | 证明新 Raid 获得全新武器状态。 |
| `CMakeLists.txt` | `WeaponFireTest` 与 target source lists | 把新增实现接入主程序和依赖它的测试目标。 |

## 4. 修改前后的执行路径

- 修改前：Space → `GameplayInput::firePressed` → `GameplayWorld` 内两个 float cooldown → Player 上次移动朝向 → 固定上方枪口 → 600 px/s、8×20 Projectile。
- 修改后：SDL mouse/keyboard → `InputSystem` held/edge/抑制 → `App::makeGameplayInput` 值快照 → `GameFlow` 只在 Raid 转发 → `GameplayWorld` 先移动 Player、再应用 aim → `WeaponFireState::update` 返回可选 `ShotSpec` → 方向枪口创建 1200 px/s、8×8 Projectile → 有界子步推进/命中/删除 → `ParticleSystem::emitImpact` → App 只读渲染准星、弹道和火花。
- 输入所有权先由屏幕和库存层仲裁；只有未被 UI 消费的 pointer held/edge 才进入 GameplayInput。领域层不接触 `SDL_Event`，App 不保存第二份 cooldown、扩散或后坐力。

## 5. 关键设计决策

1. 选择单独的 `WeaponFireState`，没有把 cooldown、扩散随机和恢复堆进 `App`。结果是领域状态可测试，新 `GameplayWorld` 自然重置一切单局射击状态。
2. `ShotSpec` 是值类型命令结果，不拥有 Projectile；只有 `GameplayWorld` 决定是否创建实体。
3. 保留 Space 一个里程碑，并与左键合并成同一个领域 fire snapshot，避免维护两套射击规则。
4. UI 点击采用“抑制直到匹配物理 release”，而不是只清当前帧 edge；否则关闭库存或完成切屏后，同一次仍按住的鼠标会开始射击。
5. 任意方向投射物使用 8×8 逻辑 AABB；火光头、辉光和拖尾只属于表现，不倒流修改命中面积。
6. 1200 px/s 后采用最多 8 px 的常规子步和 256 次上限；直接一次位移再做 AABB 会在掉帧时穿过敌人，无上限 while 又会让异常 deltaTime 形成无界工作量。
7. 命中冲击复用既有 `ParticleSystem` 与 `HitResolutionResult::hitPositions`，不新增第二个伤害事件或得分入口。

## 6. C++ 语言与标准库

- 语言特性：C++20 聚合配置、类内成员初值、`[[nodiscard]]`、`noexcept`、结构化值对象与委托构造。
- 标准库组件：`std::optional` 表达“本帧可能没有瞄准点/ShotSpec”；`std::clamp/min/max` 保护射击反馈；`std::isfinite/sqrt/sin/cos/ceil` 校验、归一化、旋转与子步计算；`std::uint32_t` 实现项目自有 xorshift 序列；`std::vector::insert` 累计子步命中值。
- `const`、引用、值、指针与 move 语义：`GameplayInput`、`ShotSpec`、`Vec2` 都按值跨层；`Projectile::velocity()` 返回小型只读值；遍历实体时引用只活在当前未变更的循环体；命中函数删除 vector 后不保留旧引用。没有新增裸拥有指针或 move-only 所有权转移。
- `noexcept` / `[[nodiscard]]`：状态查询和只做有限值赋值的 `Player::faceDirection`/射击查询使用 `noexcept`；可能被忽略会造成逻辑错误的 `update`/`faceDirection` 返回值标记 `[[nodiscard]]`，调用方显式消费或转换为 `void`。

## 7. 所有权与生命周期

- `GameFlow` 继续唯一拥有 `GameSession`，`GameSession` 唯一拥有当前 `GameplayWorld`。
- `GameplayWorld` 唯一拥有 `WeaponFireState`、Projectile vector、Enemy vector 和 ParticleSystem；新 Raid 构造新世界，因此不复制旧 cooldown/扩散/后坐力。
- App 只拥有设备级 pointer 快照与进程级 cursor 显隐缓存；`shutdown()` 兜底调用 `SDL_ShowCursor()`，避免全局光标状态泄漏。
- 子步命中可能在 `resolveProjectileEnemyHits` 内删除 Projectile/Enemy。外层不跨调用保存它们的引用、迭代器或下标，只把命中位置和击杀数复制到累计结果。
- 本轮不触碰 ItemInstance、稳定 ID 或库存 move-only 所有权。

## 8. 数据结构、算法与复杂度

- `WeaponFireState` 使用常数个 float/整数保存状态，单次 update 为 O(1)。xorshift 只依赖项目自有 32 位状态，使同配置、同调用序列可重复。
- pointer 输入用四个 bool 区分 physical held、gameplay held、edge 和 suppression；没有事件队列所有权变化。
- 弹丸子步数为 `clamp(ceil(1200 * dt / 8), 1, 256)`。每个子步遍历当前 Projectile，并调用既有 Projectile×Enemy 命中处理；最坏为 O(S×P×E)，S≤256。当前实体规模很小，且上限保护异常帧。
- 每个子步的 `HitResolutionResult` 按值追加到总结果，额外空间与本帧有效命中数线性相关。
- 渲染每枚投射物/粒子执行常数次线段和矩形绘制，分别为 O(P) 与 O(particles)。

## 9. 状态机与事务规则

- `WeaponFireState`：有效 trigger 且 cooldown 为零才产生一发；每次 world update 最多一发。首发 offset=0，后续扩散有界；松开后经过恢复延迟归零，归零后下一 burst 再次首发精确。
- 非有限/负 deltaTime 不改变武器状态、不产生 shot；零 deltaTime 允许消费一次有效输入 edge。
- InputSystem：左键 down 建立 held+edge；`endFrame()` 只清 edge；UI suppression 清玩法 held/edge，并在物理仍按住时保持抑制；匹配 up 才重新武装。失焦清所有键鼠状态。
- GameplayWorld：终局和致死接触仍优先于射击/弹丸；子步顺序固定为推进→命中删除→出界删除，最后一次性生成表现粒子和结算击杀分数。
- 失败/边界：非法 aim 保留上次有效 facing；无 `ShotSpec` 不创建 Projectile；UI 消费点击、非 Raid、库存打开和终局均不产生新弹。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终修复 | 验证 |
| --- | --- | --- | --- | --- |
| 编译环境 | 普通 PowerShell 加载 `Launch-VsDevShell.ps1` 失败，`cmake/cl` 不可见。 | 本会话环境同时存在 `Path/PATH` 键，VS PowerShell 模块合并字典失败。 | 改用 `VsDevCmd.bat` 与 Visual Studio CMake 明确路径；未把本机路径写入共享项目配置。 | 后续全目标构建成功。 |
| 构建权限 | 首次 Ninja 构建出现 C1041 PDB 与 `.ninja_lock` permission denied。 | 沙箱禁止在 E 盘 build 目录写入，不是源码编译错误。 | 以受控工作区构建权限重跑相同命令。 | 主程序、聚焦目标和全目标均成功链接。 |
| 运行 UX | 代码准星与系统鼠标同时显示。 | 只绘制了准星，没有管理 SDL 进程级 cursor 状态。 | 生命周期条件显隐并在 shutdown 兜底恢复。 | 用户确认补充验收 11–13。 |
| 视觉 UX | 首轮弹体过大、块状拖尾不明显，900 px/s 仍偏慢，命中灰粒子冲击不足。 | 表现面积集中在弹头，速度和命中反馈层次不足。 | 3×3 热芯、细长两段拖尾、1200 px/s、短寿命火光火花。 | 自动行为测试通过；最终手感验收 15 未验证。 |
| 正确性风险 | 提高弹速会让单次离散 AABB 跨过敌人。 | 一帧只在最终位置检查碰撞。 | 有界距离子步推进并按值累计命中结果。 | `FastProjectileDoesNotTunnelThroughEnemyDuringLargeFrame` 通过。 |
| 测试耦合 | 原 cooldown 测试用 0.25 秒推进后，弹丸提前命中并被删除。 | 测试把 cadence 与世界命中路径混在同一个时间假设中。 | cadence 测试改用精确 0.12 秒，命中由独立测试覆盖。 | 全量 CTest 483/483。 |
| 链接 | 未发生。 | — | — | 全目标链接成功。 |

## 11. 验证证据

- Configure：复用已成功配置的 `windows-debug` Ninja/Debug/x64-windows 构建目录；本轮无 CMake 配置输入变化。
- Build：`cmake --build --preset windows-debug --target Project_Raidline GameplayWorldTest ParticleSystemTest` 成功；随后 `cmake --build --preset windows-debug` 全目标成功。
- 目标测试：`ctest ... -R "^(GameplayWorldTest|ParticleSystemTest)\."`，57/57 通过。
- 全量 CTest：Windows Debug 并行执行 483/483 通过，用时 6.35 秒。
- 注册/接线：`ctest -N` 为 483；`compile_commands.json` 包含 `weapon_fire.cpp` 与 `gameplay_world.cpp` 的主程序/测试编译项；主程序 `gameplay_world.cpp.obj` 的 Ninja `#deps 120`。
- 其他测试：无艺术资源变化，`tests/test_phase1_assets.py` 不适用且未执行。
- CI：尚未创建 feature commit/PR，精确 head Windows/Ubuntu CI 未验证。
- 人工验收：用户确认 1–10 和 11–13；最终合并验收 15（1200 px/s、细小拖尾、醒目火光命中）未验证。

## 12. 教学分级

- 用户已接触、可快速复习：值类型 `Vec2`、`std::optional`、GameFlow/GameSession/GameplayWorld 所有权、CTest、`[[nodiscard]]`。
- 可能仍不稳定、应重点讲：输入 edge 与 held 的区别、UI suppression 到物理 release、`vector` erase 后引用失效、逻辑碰撞与视觉表现分离。
- 本次首次出现：可测试的射击领域状态、项目自有确定性伪随机序列、角度旋转、有界 projectile substeps、进程级 SDL cursor 生命周期。
- 重复样板、无需展开：CMake 每个测试 target 重复列业务源码、GTest 基本 EXPECT/ASSERT 语法、简单 getter。

## 13. 复盘问题

1. 为什么只读取 mouse held 会丢失同帧 down+up 的短点击？
2. 为什么 UI 消费左键后必须等物理 release，而不是只把本帧 edge 清零？
3. 为什么 `WeaponFireState` 返回 `optional<ShotSpec>`，而不直接持有或创建 Projectile？
4. 为什么瞄准方向要在 Player 移动之后应用？
5. 为什么任意方向弹丸不应继续使用竖直的 8×20 AABB？
6. 子步命中删除 vector 元素后，为什么只能累计 Vec2/计数值，不能保留 Projectile 引用？
7. 256 子步上限解决了什么风险，又留下了什么极端 deltaTime 权衡？
8. 为什么命中火花不能成为第二个伤害或得分事实来源？

## 14. 文件与函数定位

- `src/weapon_fire.cpp`：`WeaponFireState::update`、`recover`、`nextSignedUnit`。
- `src/input_system.cpp`：`InputSystem::handleEvent`、`suppressPrimaryPointerUntilRelease`、`endFrame`。
- `src/app.cpp`：`App::makeGameplayInput`、`processEvents`、`renderProjectiles`、`renderAimCrosshair`、`renderParticles`、`syncSystemCursorVisibility`、`shutdown`。
- `src/player.cpp`：`Player::faceDirection`。
- `src/gameplay_world.cpp`：`GameplayWorld::update` 的 aim、ShotSpec、方向枪口和 projectile substeps。
- `tests/test_weapon_fire.cpp`、`tests/test_input_system.cpp`、`tests/test_gameplay_world.cpp`、`tests/test_game_session.cpp`：合同与回归证据。
- `doc/exec-plans/active/week26-mouse-aim-shooting-recoil.md`：范围、调参、验收和剩余状态。

## 15. 技术债与测试债

- 技术债：武器参数仍为编译期硬编码；App 继续承担较多 SDL/渲染职责；多个测试 target 重复编译业务源码；大于子步上限可完全覆盖的极端 deltaTime 仍不是连续碰撞检测。
- 测试债：没有 App 自动化截图/视觉测试；系统鼠标显隐、准星、弹道与命中颜色仍依赖真实窗口；Ubuntu 只由 PR CI 验证。
- 下一安全任务：先取得最终手感验收 15，再完成精确 feature head CI 和 Week26 合入；之后按路线图为 Week27 冻结抓/挠/咬的 Windup/Active/Recovery、伤害窗口、位移与控制合同。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 doc/handoffs/completed/2026-08-08-week26-mouse-aim-shooting-recoil.md 这次任务的真实 diff、执行路径、测试与错误记录进行教学。先解释，再逐步提问；把知识分成“我已接触”“可能不稳定”“首次出现”三类，重点结合 WeaponFireState、InputSystem 的 pointer suppression、GameplayWorld 的 projectile substeps、vector 删除失效、SDL cursor 生命周期和逻辑/视觉分离，避免脱离项目的大段教材式扩展。

不要修改代码；先讲清每个设计为何存在，再用本报告第 13 节问题检查我的理解。
```

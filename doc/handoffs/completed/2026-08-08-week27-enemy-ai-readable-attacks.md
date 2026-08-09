# Week27 简单敌人 AI 与抓/挠/咬可读攻击 C++ 教学交接

## 1. 任务状态

- 日期/分支：2026-08-08，`codex/week27-enemy-readable-attacks`，基线 `main@3b77718`。
- 完成范围：确定性二维追击、近距默认 Scratch、条件诱发 Grab→Bite、空冲 OffBalance、三级移动速度、双方受击减速、单次命中与代码预警。首轮 13 项人工验收已经通过，随后根据游玩反馈完成第二轮节奏修订。
- 自动证据：第二轮 Windows Debug 全目标构建成功；全量 CTest 519/519 通过；新增源进入主程序和所有依赖 Enemy 的测试目标，未出现 Runtime Library / `gtest_ar_`。
- 人工证据：用户在真实 Windows Debug 窗口中确认第二轮 1–13 全部通过。
- 合入证据：最终提交 `4f151f1` 的 Ubuntu/Windows 精确 head CI 全部通过，PR #50 由 merge commit `520f4ec` 合入 `main`。

## 2. 玩家可见结果

- 活动 Raid 中敌人以 72 px/s 朝玩家当前中心二维追击；近距离通常选择带 0.18 秒短前摇的 Scratch，首次接敌不会 Grab。
- Scratch 真正启动后武装一次特殊机会；玩家退到 100–170 px 并连续保持 0.50 秒、且 Grab 冷却完成时，AI 才请求 Grab。离开距离带会清空保持进度。
- Grab 使用蓝青预警，0.55 秒 Windup 中继续以常速移动并追踪；进入 Active 才锁定方向，以 135 px/s 冲刺 0.55 秒。Grab 自身不造成伤害。
- Grab 抱住玩家后立即转换成红紫 Bite，造成 2 点伤害并让存活玩家受控 0.75 秒；AI 不会独立选择 Bite。空冲则进入 1.35 秒 `OffBalance`，敌人横倒且不能移动/选招。
- Player 被近战命中、Enemy 被投射物命中后，受击者获得 0.18 秒、时间倍率 0.28 的顿挫；控制优先于 Player 移动。
- Windup、Active、Recovery/OffBalance 有不同透明度；攻击名、阶段、锁向线、0/72/135 速度档位、受控和顿挫剩余时间可在 Debug 信息中观察。
- 单纯与敌人重叠不再触发旧的 0.75 秒接触伤害；只有 Active 单次窗口能扣血。

## 3. 核心文件与符号

| 文件 | 核心符号 | 职责 |
| --- | --- | --- |
| `src/enemy_attack.h/.cpp` | `EnemyAttackState`、`EnemyAttackConfig`、`EnemyAttackAdvance` | 三类配置、阶段推进、锁向、Active 时间与单次命中消费 |
| `src/enemy_ai.h/.cpp` | `EnemyAiState`、`EnemyAiDecision` | 默认 Scratch、特殊距离保持条件、武装消费与冷却 |
| `src/enemy.h/.cpp` | `updateTowardsTarget`、`EnemyMovementState`、攻击快照 getter | 唯一拥有 AI/攻击/受击减速，提交三级速度位移并夹世界边界 |
| `src/player.h/.cpp` | 控制与 `isImpactSlowed` | 唯一拥有受控/受击减速剩余时间并抑制或缩放移动 |
| `src/gameplay_world.cpp` | Enemy bounded substeps、攻击命中提交 | 提供目标值，编排碰撞、伤害、控制与致死早退 |
| `src/app.cpp` | `renderEnemyAttackTelegraphs` | 只读逻辑快照并绘制三色范围、锁向线和阶段文字 |
| `tests/test_enemy_attack.cpp` | `EnemyAttackStateTest` | 领域状态、非法输入、阶段、帧切分与消费契约 |
| `tests/test_enemy_ai.cpp` | `EnemyAiStateTest` | 首次不 Grab、默认 Scratch、中距离保持/重置/消费与非法目标契约 |
| `tests/test_enemy.cpp` | AI/攻击实体接线测试 | 三级速度、Windup 追踪、Active 锁向、OffBalance、受击减速与边界 |
| `tests/test_player.cpp` | Control/impact slow tests | 整帧控制、max 刷新、非法/死亡拒绝与减速恢复 |
| `tests/test_gameplay_world.cpp` | AI/攻击集成测试 | 首次 Scratch、完整 Grab→Bite、旧接触移除、致死早退和控制抑制 |

## 4. 修改前后的执行路径

修改前：

```text
GameplayWorld
  -> Enemy::update(deltaTime, worldWidth) 水平巡逻/反弹
  -> Player/Enemy AABB 重叠
  -> 0.75 秒 contact cooldown
  -> Player -1 HP
```

修改后：

```text
GameplayWorld（活动 Raid）
  -> Player center - Enemy center 得到值类型 targetOffset
  -> Enemy::updateTowardsTarget
       -> EnemyAiState：Normal 追击、默认 Scratch 或条件 Grab 请求
       -> EnemyAttackState：普通恢复或 Grab 空冲 OffBalance
       -> Enemy：Grab Windup 追踪常速移动；Active 锁向攻击速度
  -> Grab AABB 接触：confirmGrabContact 原子切换 Bite
  -> Scratch/Bite：先 consumeAttackHit，再提交 Player damage/control
  -> Projectile 命中 Enemy、近战命中 Player：受击者刷新 impact slow
  -> 致死立即形成 sticky PlayerDead 并停止本帧后续 mutation
  -> App 只读同一攻击快照绘制预警
```

## 5. 为什么把 AI 与攻击拆成两个状态对象

`EnemyAiState` 回答“Idle 时下一步想做什么”，输出值类型移动方向或攻击请求；它不读取 SDL、Player、碰撞容器或渲染状态。`EnemyAttackState` 回答“已经启动的动作走到哪一阶段、还能否命中”，不重新选择目标。二者都由 Enemy 唯一拥有。

这样可避免三类常见错误：App 为了画预警保存第二份阶段；GameplayWorld 与 Enemy 各保存一份 cooldown；所有攻击都在 Windup 中追踪而导致预警不可躲避。当前只有 Grab Windup 明确允许追踪，进入 Active 后立即锁向；Scratch 从启动起保持方向。

## 6. 攻击状态机与帧率独立

- `tryStart` 只允许 Idle、有效枚举、有限非零方向；成功后立即归一化。只有 Grab Windup 可用 `trackDirection` 更新，Active 后拒绝追踪。
- `update` 用阶段剩余时间消费 deltaTime；一个调用最多跨过 Windup、Active、Recovery/OffBalance，循环次数有界。
- Grab 位移不是简单的“进入 Active 就瞬移”，而是 `lungeDistance * consumedActiveTime / activeDuration`。不同帧切分会累计相同总距离。
- 浮点阶段边界使用极小 epsilon，避免 `0.01F` 重复累加留下微小 Recovery 残量。
- 大帧跨出 Active 时保留一次本次调用的 pending opportunity；GameplayWorld 再用最大 1/120 秒子步保证实际碰撞窗口可观察。

## 7. 单次命中为什么先消费再伤害

`hasAttackHitOpportunity()` 是查询，不提交状态。Scratch/Bite AABB 成功后，GameplayWorld 必须先调用 `consumeAttackHit()`，再调用 `damagePlayer()` 和 `applyControl()`。Grab 是无伤害接触机会：AABB 成功后先 `confirmGrabContact()` 转为 Bite，再消费 Bite 命中，不能把 Grab 和 Bite 分别扣血。

这个顺序和库存事务中的“先证明/保留提交条件，再移动所有权”思想相同：副作用发生前先固定唯一提交权。

## 8. 简单 AI 的确定性规则

- 未武装特殊机会时：远距返回归一化追击方向，`distance <= 76` 且 Scratch 就绪时请求 Scratch；首次接近绝不请求 Grab。
- Scratch 真正启动后：`100 <= distance <= 170` 才累计特殊保持时间，离开立即归零；累计达到 0.50 秒且 Grab 就绪时请求 Grab。
- Grab 真正启动后：消费武装并开始 4 秒冷却；Bite 不由 AI 决策，而由 Grab Active 接触在攻击状态机内转换。
- 没有可用近战且 `distance > 48`：继续接近；48 px 内停止，避免挤入玩家中心抖动。
- 只有 `recordAttackStarted` 才提交 cooldown、武装或消费；“想攻击但 Enemy 拒绝启动”不会错误消耗技能。
- 动作期间 `canStartAttack=false`，AI 只推进冷却，不输出移动或第二次攻击。
- 不使用随机数，所以相同状态、目标值和 deltaTime 得到相同结果。

## 9. Player 控制的唯一所有权

Player 保存 `controlRemaining_`；GameplayWorld 和 App 都只查询。`applyControl` 拒绝非正、NaN、Inf 和死亡玩家；重复控制取现有剩余与新时长的较大值，不相加。

Player 在 update 开始时记录本帧是否受控，再缩短计时；即使本帧刚好归零，该帧仍完整抑制移动，下一帧才恢复，避免同一 deltaTime 中“先解控还是先移动”的隐式顺序。GameplayWorld 同时抑制射击和世界 F 交互，但仍允许瞄准方向更新。

Player 与 Enemy 还分别拥有 `impactSlowRemaining_`。非致死命中刷新为 0.18 秒；每次 update 先把本帧拆成“受减速时间”和“正常时间”，前者乘 0.28，计时本身仍按真实 deltaTime 衰减。这样一个跨过减速结束点的大帧也不会把整帧都错误减速。Bite 控制优先让 Player 整帧不移动，减速计时仍可自然衰减。

## 10. 所有权、引用与容器安全

- Enemy 按值拥有 `Health + EnemyAiState + EnemyAttackState + movementState + impactSlowRemaining`；没有新增裸资源或动态分配。
- Player 按值拥有 `Health + controlRemaining + impactSlowRemaining`；构造失败由 Health 的既有验证负责。
- GameplayWorld 在敌人子步中只保留当前循环体内的 `Enemy&`；循环内不增删 `enemies_`，不跨容器 mutation 保存引用、迭代器或下标。
- 攻击 getter 返回小型值或 `std::optional` 值；App 不获得可变攻击状态。
- 新 Raid 构造新 GameplayWorld，因此 AI cooldown、攻击阶段和 Player 控制自然重置，无需外层复制布尔标志。

## 11. 本轮发现并修复的问题

| 问题 | 根因 | 修复与证据 |
| --- | --- | --- |
| 88 个 0.01 秒切片后仍残留极小 Recovery | float 阶段边界精确比较 | epsilon 阶段转换；Grab 帧切分测试通过 |
| 纯竖直追击不播放移动动画 | `Enemy::isMoving` 只检查 x velocity | 同时检查 x/y；竖直追击动画测试通过 |
| 非法强转枚举可能按 Grab 配置进入状态 | `indexOf` 的保守回退不能代替命令验证 | `tryStart` 显式拒绝非法 enum；专项测试通过 |
| 首次普通构建无法写 E 盘 Ninja/vcpkg 缓存 | 当前沙箱写权限，不是代码错误 | 经授权在真实工作区执行相同构建；全目标成功 |
| 三条旧测试仍断言水平巡逻/反弹/接触 cooldown | 产品契约已被本周 AI/攻击替代 | 改为二维追击、自动 Grab、Active 伤害/致死早退集成测试 |

## 12. 验证证据与剩余风险

- Windows Debug configure/regenerate 成功，主程序 `Project_Raidline.exe` 与全测试目标链接成功。
- `EnemyAttackStateTest + EnemyAiStateTest + EnemyTest`、Player/GameplayWorld 聚焦回归通过。
- 第二轮全量 CTest 519/519；注册数量 519。
- `EnemyAttackTest.exe` 11/11、`EnemyAiTest.exe` 8/8 与 GameplayWorld 关键集成 4/4 直接运行通过，未出现 Microsoft Runtime Library / `gtest_ar_` 栈损坏。
- `compile_commands.json` 中主程序、EnemyTest、HitResolutionTest、GameplayWorldTest、GameSessionTest、GameFlowTest 均编译 `enemy_attack.cpp` 与 `enemy_ai.cpp`。
- C++ 安全审查未发现仍需修改的所有权、引用失效、`noexcept`、`[[nodiscard]]`、状态转换或链接问题。
- 剩余风险：App 没有自动截图测试；正式攻击动画与音效尚未制作；极端非终局大 deltaTime 超出 2048 子步覆盖能力时仍是有界近似。距离感知、失去目标与多敌人协作属于 Week28。

## 13. 本轮明确未做

- 不做视野、听觉、遮挡、失去目标、仇恨、随机权重、行为树、导航网格、寻路或局部避障。
- 不做多敌人协作、包围、友方避让或远近战组合。
- 不制作正式抓/挠/咬动画、音效、击退、屏幕震动、血液或完整受伤动画；本轮只有逻辑减速、火光轮廓与横倒旋转占位。
- 不修改库存、Loot、Stash、地图内容和武器弹药系统。

## 14. 复盘问题

1. 为什么 AI 决策与攻击阶段不能合成一个每帧都重新选目标的函数？
2. 为什么攻击方向要在 Windup 开始时锁定，而不是 Active 时读取玩家当前位置？
3. 为什么 Grab 位移按 Active 消费时间计算，才能保证 30 FPS 与 120 FPS 等价？
4. 为什么 `hasAttackHitOpportunity` 之后必须先 consume，再扣血？
5. 为什么 cooldown 应在动作真正启动后提交，而不是生成 attackRequest 时提交？
6. 为什么 Bite 控制应由 Player 唯一拥有，而不是 GameplayWorld 和 App 各存一个 bool？
7. 为什么受控计时本帧归零后仍要等下一帧才恢复移动？
8. 攻击子步的 1/120 秒与 2048 上限分别解决什么问题，又留下什么极端权衡？
9. 为什么 App 可以画更亮的 Active 区域，却不能据此决定伤害？
10. 为什么 Week28 应深化感知与多敌人策略，而不是本周直接引入行为树框架？

## 15. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 doc/handoffs/completed/2026-08-08-week27-enemy-ai-readable-attacks.md 和对应真实 diff 教学。先解释，再逐步提问。重点结合 EnemyAiState 与 EnemyAttackState 的职责分离、确定性距离/冷却决策、锁定方向、阶段剩余时间、Grab 帧率独立位移、单次命中先消费后伤害、Player 控制唯一所有权、GameplayWorld 有界子步、App 只读预警，以及 vector 引用生命周期。请区分本地自动测试、真实窗口验收和精确提交 Ubuntu/Windows CI 各自证明的内容，不要把 Week28 的感知/寻路或 Week29 的正式动画说成已经实现。
```

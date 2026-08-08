# Week26 鼠标瞄准、射击与 V0 后坐力 ExecPlan

- 状态：Completed
- 负责人/工作流：主线程；`raidline-feature-delivery` + `raidline-cpp-safety-review` + `raidline-build-test-ci` + `raidline-task-closeout`
- 最后更新：2026-08-08

## 目标与玩家可感知结果

把当前“WASD 决定朝向、Space 沿移动朝向连续发射”的原型升级为适合斜俯视角战斗的鼠标入口：玩家移动与瞄准解耦，鼠标指向世界位置，左键在 Raid 中连续射击。第一发准确，持续射击逐步增加小幅扩散；松开后扩散和可视后坐力平滑恢复。代码绘制准星显示当前瞄准点和散布状态，使玩家能读懂射击反馈。

本轮建立可测试、可调参的 V0 武器射击状态，不追求最终武器系统。Space 暂时保留为键盘回归路径，避免一次切换同时删除旧控制。

## 当前仓库状态与基线

- 分支基线为 Week25 文档收口后的 `main@ff828de`；Week25 feature commit `23cd19b` 已通过 PR #46 合入，文档收口 PR #47 也已合入。
- Windows Debug 全量 CTest 为 462/462；Week25 真实窗口 1–10 与精确 head Actions run 31247705924 的 Windows/Ubuntu CI 全部通过。
- `App::makeGameplayInput()` 当前只从 InputSystem 读取 WASD、Space 和 F；SDL mouse 只服务屏幕按钮与库存，没有局内射击 held/edge 状态。
- `GameplayInput` 只有移动、`firePressed/fireJustPressed` 和交互，没有瞄准目标。
- `Player::update()` 用移动方向覆盖 `facingDirection_`；静止时保留上一次移动朝向。
- `GameplayWorld::update()` 以 0.25 秒固定间隔，沿 Player facing 生成 600 px/s、8×20 的投射物；发射位置固定在玩家上方，因此任意方向瞄准尚不成立。
- 当前窗口和世界均为固定 1280×720，没有 camera、viewport 或缩放；mouse 坐标可在 App 适配为当前世界坐标，但该等价关系不得进入领域类型的不变量。

## 冻结范围

### 本轮实现

- `GameplayInput` 增加可选的世界瞄准点；SDL 坐标只在 App 中采集/适配，核心类不接收 `SDL_Event`。
- Raid 且库存关闭时，鼠标左键按下/按住作为射击；鼠标移动持续更新瞄准，WASD 只控制移动。
- Space 继续产生同一个领域 `firePressed/fireJustPressed`；有有效鼠标瞄准时沿鼠标方向射击，没有有效瞄准时回退到 Player 上次有效 facing。
- 增加 SDL 无关的 `WeaponFireState`（最终命名可在实现时按仓库风格调整），拥有冷却、连续射击扩散、确定性偏移序列、可视后坐力和恢复状态，并返回值类型 `ShotSpec`。
- 第一发不偏移；持续射击在有界圆锥内产生可重复的左右角度偏移。停止射击后先经过短恢复延迟，再平滑回到基础扩散和零后坐力。
- Player 移动完成后使用有效 aim direction 更新 facing；瞄准不改变速度、位置或碰撞体。
- 投射物从玩家中心沿最终 shot direction 的外缘生成；逻辑碰撞 footprint 改为方向无关的小正方形，避免任意角度下沿用竖直 8×20 AABB。
- App 在 Raid 且库存关闭时绘制代码准星/散布反馈；非 Raid 与库存界面保持普通鼠标交互。
- 屏幕按钮、库存左键、切屏点击和库存开启帧会消费并抑制 pointer fire，直到物理左键释放后才能重新武装，避免 UI 点击泄漏为射击。
- 终局、非 Raid 屏幕、窗口失焦和库存打开时不产生新 shot；既有致死帧提前返回与世界冻结继续优先。

### V0 初始调参

这些数值是人工验收起点，不是长期平衡承诺；若验收中调整，必须同步测试与计划决策日志。

| 参数 | 初始值 | 玩家感知 |
| --- | ---: | --- |
| 连射间隔 | 0.12 秒 | 按住左键可连续射击 |
| 投射物速度 | 1200 px/s | 900 px/s 与细小弹体修订后仍被反馈偏慢；最终提高 33%，由细长拖尾和醒目命中火花承担可读性 |
| 首发扩散 | 0° | 第一次射击准确 |
| 每发扩散增长 | 1° | 持续射击逐步变散 |
| 最大扩散 | 6° | 偏移有明确上限 |
| 恢复延迟 | 0.10 秒 | 短点射可读 |
| 扩散恢复 | 12°/秒 | 松开后快速但非瞬时收束 |
| 可视后坐力增长/上限 | 3 px / 9 px | 准星反馈短促外扩 |
| 可视后坐力恢复 | 45 px/秒 | 停火后平滑归零 |
| 投射物逻辑 footprint | 8×8 | 任意方向使用一致 AABB |

### 明确不做

- 不实现弹匣、换弹、备弹消耗、9mm 与武器绑定、武器拾取/装备、切枪、改装、射速属性表或多武器平衡。
- 不实现相机、屏幕震动、时间缩放、命中停顿、手柄瞄准、辅助瞄准或网络同步。
- 不生成最终准星、枪口火焰、弹道、武器或 UI 美术；本轮只使用 SDL 代码绘制反馈。
- 不实现敌人抓/挠/咬、敌人 AI、受伤击退或 Week29 的整体战斗节奏收口。
- 不顺带修复角色纯上/下移动动画、库存 #38/#39、App 大拆分或 CMake 核心 library 重构。

## 主要类型、调用路径与所有权

```text
SDL mouse motion/button + keyboard
  -> App / InputSystem
       screen & inventory arbitration
       pointer position -> optional world aim point
       pointer/Space -> unified fire held + edge
  -> GameplayInput (value snapshot, SDL independent)
  -> GameFlow (only Raid forwards)
  -> GameSession
  -> GameplayWorld
       Player moves
       world aim point -> normalized aim direction
       Player facing follows valid aim
       WeaponFireState updates cooldown/bloom/recoil
       optional ShotSpec -> spawn Projectile at directional muzzle
  -> App reads fire feedback and renders code crosshair
```

`GameplayWorld` 唯一拥有单局 `WeaponFireState`，因此新 Raid 自动获得干净的冷却、扩散、后坐力和确定性序列。`ShotSpec` 只携带本帧最终方向与反馈值，不拥有 Projectile。App 只保留设备级 pointer 位置、held/edge/抑制状态，不保存第二份武器冷却或扩散。

## 新增与受影响不变量

- aim world position 是可选有限值；从玩家中心到目标的向量必须有限且长度大于 epsilon 才能成为新 facing/shot direction。
- 鼠标位于玩家中心或输入非有限时，不产生 NaN/Inf；保留上一次有效 facing，若从未有有效 aim 则使用初始向上方向。
- WASD 只决定移动向量；有效 aim 独立决定 Player facing 与 shot 基础方向。
- `WeaponFireState::update/query` 不生成实体；只有成功返回 `ShotSpec` 后 GameplayWorld 才新增一个 Projectile。
- 第一发精确；后续偏移由项目自有确定性整数序列产生，不依赖跨标准库不稳定的 `uniform_real_distribution` 具体结果。
- cooldown、扩散、后坐力和恢复量始终有限并 clamp 在配置范围；非正或非有限 deltaTime 不推进恢复或冷却。
- 一次 update 最多按已定义的 cadence 生成一发，不用 while 对大 deltaTime 补发历史子弹，避免切屏或卡顿后爆发生成。
- 投射物速度方向归一化且非零；spawn 位于玩家碰撞体外缘，不能因朝向固定写在玩家上方。
- 非 Raid、Raid 终局、库存打开、屏幕转换帧和 pointer fire 抑制期不得生成投射物。
- UI 消费的左键按下必须等待对应释放后才能重新武装；不能在关闭库存或部署后的下一帧把仍按住的同一次点击解释为射击。

## 分阶段实施与退出条件

1. 领域射击状态：新增配置、状态、确定性偏移和 `ShotSpec`。退出条件：专用测试覆盖首发、cadence、扩散上限、恢复、确定性、非法配置/输入与大 deltaTime。
2. Gameplay 输入与世界接线：增加可选 aim world point，解耦移动/facing，方向化 muzzle 与 8×8 Projectile。退出条件：核心测试覆盖四向/斜向鼠标目标、移动中反向瞄准、中心回退、Space 回归、终局不射击和命中链不回归。
3. SDL 输入仲裁：App/InputSystem 跟踪 mouse held/edge、屏幕/库存抑制和失焦释放，保持 GameFlow 转换帧消费。退出条件：值状态测试证明 UI down 不武装、release 后才可射击、左右键和 Space 互不污染。
4. 反馈渲染与调参：代码绘制准星、扩散半径与可视后坐力，按真实窗口观察调整 V0 表。退出条件：准星只在 Raid 游戏态显示，瞄准/连射/恢复可读，不遮挡库存。
5. 收口：C++ 安全审查、Windows Debug 聚焦/全量测试、真实窗口验收、单一 feature PR 与一次精确 head CI。

## 自动测试矩阵

- 默认 WeaponFireState 无 cooldown、零扩散、零后坐力；非法或非有限配置拒绝构造。
- 首次 held/just-pressed 立即产生精确 shot；冷却内不产生第二发，达到 cadence 后只产生一发。
- 同种子/同输入序列得到相同角度偏移；偏移始终位于当前扩散圆锥，连续射击 clamp 到 6°。
- 松开后 0.10 秒内保持，之后按速率恢复且不穿过零；非正/非有限 deltaTime 不改变状态。
- 鼠标四向和斜向目标生成归一化速度；spawn 位于对应玩家外缘，8×8 bounds 和出界删除正确。
- 鼠标在玩家中心、非有限坐标与无 aim 输入均不污染 facing；Space 使用有效 aim 或上次 facing。
- WASD 向左移动同时向右瞄准时，位置向左、facing/shot 向右。
- 屏幕按钮、库存拖拽、Tab 开关、Esc、切屏帧、失焦和终局不会生成 pointer shot；release 后的新点击可恢复。
- 既有 Projectile、HitResolution、Player、GameplayWorld、GameFlow、Raid、库存和跨局测试全部通过。

预计新增或重点运行 `WeaponFireTest`、`InputSystemTest`、`PlayerTest`、`GameplayWorldTest` 与 `GameFlowTest`；最终命令和 CTest 数量以实时 CMake 与 `doc/engineering/BUILD_AND_TEST.md` 为准。用 `ctest -N`、compile database 和 Ninja deps 证明新源码进入主程序与测试。本轮无艺术资源变化，Phase1 pytest 不适用。

## 人工验收草案

1. MainMenu 和 Base 的左键按钮仍只切屏，不生成或预装一发射击。
2. 进入 Raid 后移动鼠标，角色无需 WASD 即可朝向鼠标；鼠标在上下左右和斜方向都稳定。
3. 单击左键第一发沿准星方向准确生成；按住左键按固定 cadence 连射。
4. WASD 移动与瞄准解耦：可向左移动同时向右射击，斜向移动速度不回归。
5. Space 仍能射击；有鼠标 aim 时沿鼠标方向，无有效 aim 时沿上次有效 facing。
6. 持续射击时准星扩散与后坐力反馈逐步增大但有上限；短点射更集中，松开后平滑恢复。
7. Tab 打开玩家背包或柜体界面后，点击/拖动物品不会射击；关闭时仍按住的旧点击不会泄漏，释放后新点击才射击。
8. 射击仍能命中、伤害并击杀敌人；玩家致死帧、撤离、超时和 RaidResult 后不生成新弹。
9. 返回 Base 并部署下一 Raid 后，冷却、扩散、后坐力和准星状态全部重置，Stash/流程能力不回归。
10. 全流程无 Microsoft Visual C++ Runtime Library / `gtest_ar_` 错误；准星和弹道没有明显抖动、NaN 跳变或卡在 UI 上。

用户已于 2026-08-08 确认以上 1–10 全部通过。验收后新增发现“系统鼠标与代码准星同时存在”和“白色方块弹体不够流线、速度偏慢”，因此追加以下修订验收：

11. 活动 Raid 且库存关闭时只显示代码准星；MainMenu、Base、库存打开、终局和程序退出时系统鼠标正常恢复。
12. 子弹显示为稍大的亮色像素弹头和沿实际飞行反方向的分段拖尾；上下左右与斜向均能读出方向，不再只是白色方块。
13. 900 px/s 子弹明显比原版更利落，同时仍能稳定观察、命中并击杀敌人，准星、射速和既有 1–10 行为不回归。

用户已于 2026-08-08 确认补充 11–13 全部通过，但进一步指出首轮火光弹头仍偏大、块状拖尾不明显，因此追加最终视觉验收：

14. 子弹主体是细小、明亮的火光像素，红橙到金黄的细长拖尾在四向和斜向射击时都清楚可见；弹头不再遮挡目标，900 px/s 速度与命中保持正常。

用户未明确确认 14，而是继续要求缩短弹丸飞行时间、增强速度与命中冲击力。因此以以下合并项替代 14 作为最终验收：

15. 1200 px/s 子弹在近中距离内飞行明显更短，细小弹头与火光拖尾仍可辨认；正常帧率与一次 0.30 秒大帧间隔都能命中敌人，命中瞬间出现短促的白热—金黄—红橙放射火花，且伤害、得分和射速不回归。

用户已于 2026-08-08 确认最终合并验收 15 通过。

## 风险、替代方案与失败语义

- 风险：把 mouse world position 直接写入 Player 会混合设备、移动与射击职责。选择 GameplayInput 值快照 + GameplayWorld 归一化 + Player 受控 facing 命令。
- 风险：把左键直接永久映射为 Fire 会使按钮或库存点击在切屏后泄漏。选择显式抑制/重新武装状态，并要求 release 边界测试。
- 风险：任意方向仍使用 8×20 AABB 会让水平/斜向命中范围不一致。V0 改为 8×8 方向无关逻辑 footprint；最终视觉弹道留待独立美术。
- 风险：随机扩散导致测试脆弱。使用项目自有确定性整数序列，只测试稳定不变量和相同序列复现，不依赖标准库 distribution。
- 风险：在 App 同时实现冷却、扩散与准星会形成第二事实源。所有武器时间状态由 GameplayWorld 内的领域对象唯一拥有，App 只读快照。
- 回滚：WeaponFireState、aim 字段、App pointer 适配与代码准星可作为一个切片回退；不迁移库存、Stash、GameFlow 或 ItemInstance 数据。

## 进度记录

- 2026-08-08：Week25 通过 PR #46 合入 `main@08e4475`；按路线图进入 Week26 规划。
- 2026-08-08：核对当前 Space→GameplayInput→Player facing→GameplayWorld 固定发射路径，冻结鼠标 aim、左键仲裁、确定性扩散、V0 可视后坐力和方向无关投射物边界。
- 2026-08-08：完成 `WeaponFireState`、鼠标/Space 统一射击快照、移动与瞄准解耦、方向枪口、8×8 投射物和代码准星；UI 点击采用“直到物理松开”的射击抑制。
- 2026-08-08：Windows Debug 完整构建成功；补充右键隔离、Space 不受 pointer 抑制和跨 Raid 武器状态重置后，最终 CTest 482/482 通过，等待真实窗口 1–10 验收。
- 2026-08-08：用户确认原 1–10 全部通过；随后反馈代码准星与系统鼠标重复、方块弹体缺少流线感且速度偏慢。
- 2026-08-08：本地修订为活动战斗隐藏系统鼠标、UI/终局/退出恢复；弹体速度提高到 900 px/s，增加亮色像素弹头、辉光与四段方向拖尾。全目标构建和 CTest 482/482 再次通过，等待补充 11–13。
- 2026-08-08：用户确认补充 11–13 全部通过，但继续反馈首轮弹头偏大、拖尾不明显。第二轮纯渲染调优改为 3×3 热芯、5×5 微光、方向短弹头、两段细长火光线和小火星；Project_Raidline 构建与 CTest 482/482 再次通过，等待最终 14。
- 2026-08-08：最终 14 尚未明确确认时收到进一步手感反馈：900 px/s 飞行仍偏长、命中冲击不突出。第三轮修订提高到 1200 px/s，增加最多 8 px 常规步长、256 次上限的高速弹丸子步进，并把默认命中反馈改为 16 枚更快、更短的火光火花；Windows Debug 全目标构建、聚焦 CTest 57/57 和全量 CTest 483/483 通过，等待最终 15。
- 2026-08-08：完成 C++ 安全复核与教学交接 `doc/handoffs/completed/2026-08-08-week26-mouse-aim-shooting-recoil.md`；未发现可操作的所有权、生命周期或 vector 失效问题，保留最终手感验收 15 与精确 feature head CI 为未验证。
- 2026-08-08：feature commit `3a52354` 的 [Actions run 31260317298](https://github.com/Disthinker/Project-Raidline/actions/runs/31260317298) 全部通过：范围检测 6 秒、Ubuntu 1 分 23 秒、Windows 3 分 15 秒。用户随后明确确认最终验收 15 通过；PR #48 按精确 head 合入，merge commit 为 `0847da0`，Week26 满足 DoD 并归档。

## 发现记录

- 当前 1280×720 window 与 world 1:1 且无 camera，但坐标等价只属于 App 适配现状；领域接口使用 world point，为未来 camera 留出边界。
- 当前 projectile spawn 始终位于玩家上方，即使 velocity 已按 facing 改变；任意方向射击必须同时修正 spawn，而不能只改速度。
- Player facing 当前由移动更新；鼠标瞄准若不在移动之后提交，会被同帧 WASD 覆盖。
- 库存已经依赖左键 down/motion/up，Week26 的主要集成风险是输入所有权与跨屏/跨 overlay 泄漏，而不是单纯监听 SDL_BUTTON_LEFT。
- SDL 可能在同一帧交付左键 down 与 up；世界层必须同时接受 `fireJustPressed`，否则极短单击会因帧末 held 已清除而丢失。
- 代码准星若不同时管理系统鼠标可见性，会在活动战斗中形成两个瞄准标识；显隐必须跟随 Raid/UI 生命周期，并在 shutdown 恢复全局 SDL cursor 状态。
- 提高 Projectile 速度会让以“0.25 秒后仍留在容器”为前提的 cooldown 测试提前发生真实命中；cadence 测试改用 0.12 秒隔离时间状态，命中测试继续单独覆盖碰撞链。
- 首轮以多个放大的矩形表达拖尾时，视觉面积集中在弹头附近，实际观看更像大方块；更清晰的像素弹道需要缩小热芯，并用沿 velocity 方向的连续细线拉开亮度和长度层次。
- 直接把离散弹丸提高到 1200 px/s 会放大大帧间隔穿透 50×50 Enemy 的风险；世界层需要把单帧弹丸位移拆成有限子步，并在每次可能删除 Projectile/Enemy 后只累计命中位置和击杀数量值。

## 决策日志

- 2026-08-08：Space 保留一个里程碑作为回归路径；左键与 Space 合并为同一领域 fire snapshot，不建立两套射击逻辑。
- 2026-08-08：后坐力 V0 采用代码准星/瞄准反馈位移，不移动玩家、不引入 camera shake；更强表现留到 Week29。
- 2026-08-08：首发精确，持续射击才增加确定性扩散；投射物逻辑 footprint 统一为 8×8。
- 2026-08-08：不把 Ammo9mm 库存数量接入射击消耗；弹匣/换弹需要独立武器与装备合同。
- 2026-08-08：系统鼠标只在活动 Raid、库存关闭且代码准星可用时隐藏；所有非战斗/UI/终局路径以及 shutdown 必须恢复。
- 2026-08-08：投射物逻辑 AABB 继续保持 8×8；流线型表现只读取 `Projectile::velocity()` 绘制反向像素拖尾，不把视觉尺寸倒流为碰撞规则。
- 2026-08-08：最终速度采用 1200 px/s；弹丸推进以 8 px 为常规最大步长并限制为最多 256 个子步，兼顾高速命中可靠性与异常大 deltaTime 下的有界工作量。
- 2026-08-08：命中冲击继续复用既有 ParticleSystem，不引入第二伤害事件；只调整默认粒子数量、速度、寿命、尺寸和 App 火光渲染。

## 最终结果、验证与偏差

Week26 功能由 feature commit `3a52354` 完成。领域状态由 `GameplayWorld` 内唯一的 `WeaponFireState` 持有；App 只适配 SDL pointer、仲裁 UI 所有权并读取反馈绘制准星。用户确认原 1–10、补充 11–13 与最终合并验收 15 全部通过。Windows Debug 全目标构建成功，聚焦 CTest 57/57、全量 CTest 483/483；compile database 与 Ninja `#deps 120` 证明实现进入主程序和测试目标。精确 head Actions run 31260317298 的范围检测、Ubuntu 与 Windows 全部通过；PR #48 合入 `main@0847da0`。C++ 安全复核无可操作发现，中文教学交接已生成。

偏差：投射物调参按三轮人工反馈从 700→900→1200 px/s，增加有限子步与火光命中表现；这些均保持在 Week26 射击手感范围内。未引入弹药消耗、换弹、武器装备、相机震动、音效、敌人攻击或 AI。

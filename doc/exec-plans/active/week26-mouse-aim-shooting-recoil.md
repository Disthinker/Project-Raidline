# Week26 鼠标瞄准、射击与 V0 后坐力 ExecPlan

- 状态：Ready
- 负责人/工作流：主线程；`raidline-feature-delivery` + `raidline-cpp-safety-review` + `raidline-build-test-ci` + `raidline-task-closeout`
- 最后更新：2026-08-08

## 目标与玩家可感知结果

把当前“WASD 决定朝向、Space 沿移动朝向连续发射”的原型升级为适合斜俯视角战斗的鼠标入口：玩家移动与瞄准解耦，鼠标指向世界位置，左键在 Raid 中连续射击。第一发准确，持续射击逐步增加小幅扩散；松开后扩散和可视后坐力平滑恢复。代码绘制准星显示当前瞄准点和散布状态，使玩家能读懂射击反馈。

本轮建立可测试、可调参的 V0 武器射击状态，不追求最终武器系统。Space 暂时保留为键盘回归路径，避免一次切换同时删除旧控制。

## 当前仓库状态与基线

- 分支基线为 Week25 合入后的 `main@08e4475`；Week25 feature commit `23cd19b` 已通过 PR #46 合入。
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
| 投射物速度 | 700 px/s | 比现有原型略快、降低鼠标瞄准迟滞感 |
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

以上条目在用户实际执行前均为“未验证”。

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

## 发现记录

- 当前 1280×720 window 与 world 1:1 且无 camera，但坐标等价只属于 App 适配现状；领域接口使用 world point，为未来 camera 留出边界。
- 当前 projectile spawn 始终位于玩家上方，即使 velocity 已按 facing 改变；任意方向射击必须同时修正 spawn，而不能只改速度。
- Player facing 当前由移动更新；鼠标瞄准若不在移动之后提交，会被同帧 WASD 覆盖。
- 库存已经依赖左键 down/motion/up，Week26 的主要集成风险是输入所有权与跨屏/跨 overlay 泄漏，而不是单纯监听 SDL_BUTTON_LEFT。

## 决策日志

- 2026-08-08：Space 保留一个里程碑作为回归路径；左键与 Space 合并为同一领域 fire snapshot，不建立两套射击逻辑。
- 2026-08-08：后坐力 V0 采用代码准星/瞄准反馈位移，不移动玩家、不引入 camera shake；更强表现留到 Week29。
- 2026-08-08：首发精确，持续射击才增加确定性扩散；投射物逻辑 footprint 统一为 8×8。
- 2026-08-08：不把 Ammo9mm 库存数量接入射击消耗；弹匣/换弹需要独立武器与装备合同。

## 最终结果、验证与偏差

计划为 Ready；尚未修改业务代码，未新增测试，人工验收与 Week26 CI 均未执行。

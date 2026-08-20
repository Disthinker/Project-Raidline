# Combat：逻辑弹道与落点反馈 v1 ExecPlan

状态：已接受并合入 main（PR #66，merge commit `7877d71`）

基线：`origin/main@755fa00`（PR #65 已通过用户正常游玩验收并合入）

分支：`codex/combat-logical-ballistics-feedback-v1`

产品输入：外部只读 GDD 的 `systems/05_战斗命中与反馈.md`、`systems/11_UI交互与信息反馈.md`、`systems/12_美术动画特效与声音.md`，以及当前仓库射击领域不变量。GDD 保持只读。

## 1. 玩家结果

每次成功击发在开火时冻结本发起点、方向、实际散布和最终落点。子弹不再作为可渲染、可碰撞的场景实体存在，而以短生命值逻辑飞行记录按现有 1200 世界单位/秒直线推进；玩家在弹道飞行期间移动鼠标不会改变这一发。飞行线段连续扫掠当前敌人位置，最先接触的活目标形成领域 `HitResult`；未命中目标时，到达冻结落点后形成 `World HitResult` 和代码地面粒子。短轨迹只显示已经飞过的区段，普通命中不显示准星 X，爆头或弱点继续使用领域专用反馈。

## 2. 权威状态与合同

- `ShotCommand` 新增本发最大飞行距离；`ShotResolution` 冻结规范化方向、速度、距离和最终落点。非法距离与溢出落点明确拒绝。
- `LogicalBallisticFlight` 是 `GameplayWorld` 内部的值类型短生命值记录；它没有 sprite、场景 transform、碰撞组件、稳定资产 ID 或存档身份。
- 逻辑推进返回本帧实际飞过的起止线段。非正或非有限 delta 不推进；到达终点时精确钳制，不越过落点。
- 命中解析使用线段对目标包围盒的连续扫掠并选择最近活目标；不依赖离散弹丸当前位置、渲染帧或敌人容器顺序。
- 敌人命中和世界落点都形成 `HitResult`。伤害、命中部位、爆头/弱点、击杀和表现继续只消费领域结果。
- 本发飞行不进入 Profile、RaidSnapshot 或存档。关闭程序仍按既有规则恢复出击前 Profile，不新增 Raid 中途续玩。

## 3. 输入与表现

- 鼠标瞄准时，本发飞行距离取击发瞬间准星距离并限制在世界边界内；散布只改变该发冻结方向，不允许后续鼠标输入修正。
- Space 无鼠标目标的历史回归路径继续沿最后朝向飞到世界边界。
- App 只读取 `ShotPresentationSnapshot`；轨迹长度钳制为实际已飞行距离，不能预画未来路径。
- 敌人命中继续生成既有代码 impact 粒子；未命中在冻结落点生成同类代码地面反馈。
- 普通命中与 World 命中均不生成准星 X；只有 `Headshot`/`WeakPoint` 语义启动专用短促标记。
- 不生成、发布或接入新图像/音频，不修改美术 manifest。

## 4. 明确排除

- 完整动态准星、完全手动压枪、右键开镜、高倍率视野和射击模式扩展。
- 多目标贯穿、穿甲弹、掠射、压制、强击退、断肢、血液、碎块和尸体。
- 当前固定地图尚无正式墙/门弹道阻挡提供者；v1 只解析活敌人与冻结世界落点，不创建无消费者的通用障碍框架。
- 武器型号弹速差异、弹速内容迁移、下坠、风偏和自然减速。
- 正式枪口火光、轨迹、命中、地面粒子与声音资产生产。

## 5. 自动化与人工门槛

- 领域：合法/非法最大距离、冻结终点、溢出拒绝和稳定状态名。
- 逻辑飞行：分帧一致、终点钳制、非有限时间拒绝、完成后稳定停留。
- 命中：高速大帧不穿透薄目标、最近目标优先、一次最多命中一个活目标、头/躯干/腿语义保持。
- 世界：击发后改动鼠标不改变本发终点；敌人命中消费飞行；未命中只在到达时产生一个 World HitResult 与粒子。
- 表现：投影不携带伤害权威；轨迹不超过已飞距离；普通/World 命中不触发专用准星标记。
- 回归：WeaponAmmo、武器故障、多武器、敌人 AI/攻击、ProfileCombat、GameSession、GameFlow 和完整 CTest。
- 交付：Windows Debug 全目标、全量 CTest、exact-head Windows/Ubuntu CI。最后由用户正常游玩验收，开发代理不启动游戏。

## 6. 风险与回滚

- 最大风险是弹药已消费但逻辑飞行未创建，或飞行命中后伤害/反馈重复提交。GameSession 仍只在 `shotFiredLastUpdate` 成功后提交击发候选，每个 ShotId 只由一次命中或一次 World 到达终结。
- 当前敌人先推进、弹道后读取到达时位置；未来固定 60 Hz 全世界模拟切片可统一时序，但本轮连续扫掠已经消除高速离散穿透。
- 回滚到 `origin/main@755fa00` 可恢复旧 V0 Projectile 适配器；不涉及内容、Profile 或存档迁移。

## 7. 进度

- 2026-08-20：PR #65 通过用户验收，以 merge commit `755fa00` 合入 main。
- 2026-08-20：从精确主线建立本分支，冻结非实体延迟弹道、落点反馈和本轮排除范围。
- 2026-08-20：`LogicalBallisticFlight`、冻结终点、连续扫掠、World HitResult、已飞轨迹投影与旧 Projectile 生产路径退场完成首轮接线。
- 2026-08-20：专项目标编译成功；ShotResolution/LogicalBallistics/HitResolution/GameplayWorld/GameFlow 及相邻领域/会话回归 175/175 通过，开发代理未启动游戏。
- 2026-08-20：Visual Studio Developer Shell、x64 host/x64 target 下 Windows Debug 全目标构建成功；全量 CTest 721/721 通过。
- 2026-08-20：PR #66 exact-head Windows/Ubuntu CI 与用户正常游玩验收通过，以 merge commit `7877d71` 合入 main。

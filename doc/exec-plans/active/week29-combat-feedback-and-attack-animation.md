# Week29 战斗反馈、节奏与抓/挠/咬动画 ExecPlan

- 状态：Ready
- 负责人/工作流：主线程负责代码与 art-control；`raidline-feature-delivery` + `raidline-cpp-safety-review` + `raidline-build-test-ci` + `raidline-art-pipeline` + `raidline-task-closeout`
- 最后更新：2026-08-09

## 目标与玩家可感知结果

在 Week26 射击 V0、Week27 攻击动作规则和 Week28 多敌人协作之上，把战斗从“规则可读、调试图形可见”提升为“命中、受伤和三种敌人攻击在正常游玩中一眼可辨”：

- 开枪瞬间在枪口方向出现短促火光，命中敌人时准星给出短暂确认；这些反馈不改变投射物、伤害或 cadence 的唯一事实来源。
- 玩家受伤时出现短暂、强度与攻击类型相关的屏幕边缘脉冲；Scratch 与 Bite 的反馈强度不同，但不复制 Player HP、控制或攻击命中状态。
- Grab、Scratch、Bite 使用各自的正式双朝向攻击 sheet，并严格按现有 Windup/Active/Recovery 阶段采样；攻击逻辑和动画不能各自维护第二套时钟。
- 正式资产尚未批准时，现有代码预警和默认敌人图保持完整 fallback，程序不能因为缺少新 PNG 无法启动。
- Week28 的单主攻、搜索/支援、Week27 的 Grab→Bite/OffBalance、Week26 的射击与全部库存/Raid 流程不回归。

## 当前仓库状态与基线

- Week28 功能提交 `07755f6` 已由 PR #52 合入 `main@c4658e7`；Windows Debug 全目标构建、全量 CTest 544/544、真实窗口 1–13 与 GitHub Actions run 31300301471 全部通过。
- EnemyAttackState 唯一拥有攻击类型、阶段、方向、剩余时间和单次命中消费；Enemy 对外提供只读 attack/config 快照。
- App 当前用默认敌人移动 sheet、颜色矩形、锁向线和 OffBalance 旋转表达攻击，尚未加载独立 Grab/Scratch/Bite sheet。
- WeaponFireState 唯一拥有 cadence、扩散和可视后坐力；GameplayWorld 已在命中时创建粒子，Player/Enemy 各自拥有受击顿挫。
- 当前美术清单只把默认敌人移动 sheet 记为 auxiliary generated reference；Phase 1 未批准敌人攻击动画。Week29 显式只重开 `enemy_default_attacks_v1` 包，不开放服装、伤害层、尸体或 VFX 大包。

## 冻结范围

### 代码侧实现

1. 新增 SDL 无关的攻击表现采样值类型：输入攻击类型、阶段、阶段剩余时间与只读配置，输出是否使用攻击 sheet、0–5 帧索引、颜色/强调强度和阶段进度。它不拥有计时，不推进攻击，不决定命中。
2. 三张 sheet 均固定 6 帧、两行：第一行为左朝向，第二行为右朝向；每帧 256×320，整张 1536×640，运行时仍绘制 64×80，pivot `(128,300)` / `(32,75)` 与现有移动合同一致。
3. Scratch：Windup 使用前 2 帧，Active 使用中间 3 帧，Recovery 收束到最后 1 帧。Grab：Windup 逐步前倾，Active 使用突进帧，未命中后继续使用现有 OffBalance 横倒表现。Bite：由 Grab 接触转换后的 Active 开始采样，Recovery 收束；AI 仍不能独立选择 Bite。
4. 新增 GameplayWorld 唯一拥有的短时 `CombatFeedbackState` 或等价小对象，只保存可视反馈计时：枪口火光、命中确认、玩家受伤脉冲。命中/伤害命令成功后才触发；终局、新 Raid 和非 Raid 冻结遵守既有生命周期。
5. App 只读反馈快照：枪口短火光、准星命中刻线、玩家受伤边缘脉冲；保持系统鼠标/代码准星仲裁和库存输入抑制。
6. 第一轮不改变子弹伤害、敌人 HP、Scratch/Bite 伤害、控制时长、主攻 token、武器 cadence 或 Projectile 逻辑尺寸。手感调优优先调整纯表现时长、亮度、帧采样和既有可视后坐力曲线。

### 正式资产生产包

- 包 ID：`enemy_default_attacks_v1`。
- coherent family：默认敌人的 Grab、Scratch、Bite 三张双朝向攻击 sheet。
- 每个动作生成 2 个独立候选；每个 distinct candidate 必须使用独立 image-generation call。
- 生产任务唯一写入 `art/work/enemy_default_attacks_v1/`，保留 exact prompts、raw、透明 sheet、QA 与 `result.json`；不得修改 manifest 或发布到 `assets/`。
- art-control 负责视觉审核、最多一次精确修复、技术 QA、选择、版本化发布和 manifest 更新。
- 计划发布路径：
  - `assets/characters/enemy/default/enemy_default_attack_grab_6f_1536x640_v1.png`
  - `assets/characters/enemy/default/enemy_default_attack_scratch_6f_1536x640_v1.png`
  - `assets/characters/enemy/default/enemy_default_attack_bite_6f_1536x640_v1.png`
- 现有 `enemy_default_move_horizontal_6f_1536x640.png` 不覆盖、不改写，继续作为缺失攻击 sheet 时的 fallback。

### 明确不做

- 不实现音效、音乐、语音、完整相机/视口系统、屏幕震动、全局 hit-stop、击退、布娃娃、血液、断肢、尸体、伤害贴图或新的玩家/敌人身份。
- 不改变命中判定、攻击伤害、玩家 HP、敌人数量、AI 感知/协调、武器弹匣/换弹/弹药消耗。
- 不开放服装层、武器附件、地图建筑、程序地图或大尺寸 raster UI。
- 不让 App、动画采样器或美术帧决定命中、伤害、控制、攻击 token 或 Raid 终局。

## 主要类型与调用路径

```text
EnemyAttackState (owned gameplay clock)
  -> Enemy read-only attack snapshot
  -> sampleEnemyAttackPresentation(type, phase, remaining, config)
       -> frame index / phase progress / visual emphasis values
  -> App chooses approved attack sheet or movement-sheet fallback

GameplayWorld command result
  -> shot created      => feedback.onShot(origin, direction)
  -> projectile hit   => feedback.onEnemyHit(position)
  -> melee hit player => feedback.onPlayerHit(type)
  -> CombatFeedbackState::update(dt)
  -> App reads timers/positions and renders code feedback
```

攻击阶段和 CombatFeedbackState 都只能由 GameplayWorld/Enemy 领域层拥有；App 不保存第二份时间。正式 PNG 只替换表现输入，不改变上述命令路径。

## 新增与受影响不变量

- 攻击表现采样是纯查询；相同 attack snapshot 必须得到相同帧，不读取 SDL ticks 或独立 deltaTime。
- 帧索引始终位于 `[0,5]`；非法枚举、缺失类型、Idle 和非有限阶段值安全退化为 fallback。
- 动画 source rect 使用完整 256×320 帧和固定行语义，不裁剪 pivot/footline；运行时绘制保持 64×80。
- 反馈只在命令成功后触发：无 shot 不得显示枪口火光，无命中不得显示 hit confirm，无伤害不得显示玩家受伤脉冲。
- 反馈计时必须有限、非负、有界；负/NaN/Inf deltaTime 不推进或污染状态。
- Raid 终局后不产生新反馈事件；非 Raid 屏幕冻结，新的 GameplayWorld 得到干净反馈状态。
- 缺失、尺寸错误或加载失败的攻击 sheet 使用既有默认敌人贴图和代码预警；不能形成空敌人或初始化失败。
- 正式资产只有 art-control 批准并更新 manifest 后才能由 runtime 路径引用；runtime 永不读取 `art/work/`。

## 分阶段实施与退出条件

1. 代码合同：实现攻击表现纯采样与测试，补充阶段边界、非法输入和帧索引回归。
2. 反馈状态：实现枪口、命中确认和玩家受伤脉冲的领域计时与世界事件接线，覆盖成功/无事件/终局/新 Raid。
3. App fallback 接线：先使用现有敌人 sheet 验证 source rect、阶段采样和代码反馈，保持无新资产可运行。
4. 美术生产：由 art-control 写 package brief/JSON contract/exact prompts，并在用户明确允许创建独立生产任务后启动 `enemy_default_attacks_v1`。
5. 审核发布与最终接线：技术/视觉 QA 通过后发布三张版本化 sheet、更新 manifest、加载并替换 fallback。
6. 调优收口：真实窗口检查攻击读秒、反馈强度、多人战斗可读性和成功/失败流程；完成全量测试与精确 head CI。

## 自动测试矩阵

- 三种攻击各阶段起点、中点、终点映射到合法且单调的 0–5 帧；不同帧切分只要快照相同，采样结果相同。
- Idle、无攻击类型、非法枚举、NaN/Inf/负剩余时间均返回安全 fallback。
- Shot/EnemyHit/PlayerHit 分别触发独立计时；零/负/非有限 deltaTime 不污染；计时到期归零。
- Scratch 与 Bite 玩家脉冲强度可区分；重复事件采用明确的 max/刷新规则，不无限累加。
- 射击被 UI/控制抑制、投射物未命中、攻击窗口未碰撞时不产生对应反馈。
- Raid 终局冻结反馈事件；MainMenu/Base/RaidResult 不推进；新 Raid 重置。
- 攻击 sheet 加载失败 fallback、source rect 行列与 texture 尺寸验证有自动或可复现检查。
- Week26–28 WeaponFire、EnemyAttack、EnemyAi、EnemySquad、GameplayWorld、GameSession、GameFlow 与全部库存/Raid CTest 不回归。

## 人工验收草案

1. 开枪瞬间可看到方向正确、短促且不过大的枪口火光；停止射击后立即消失。
2. 子弹命中敌人时准星出现短暂命中确认；未命中、UI 点击和受控禁止射击时不出现。
3. 玩家被 Scratch 命中时出现清楚但不遮挡战场的边缘脉冲。
4. Bite 的脉冲比 Scratch 更强，仍可看清敌人与逃生方向。
5. Scratch 的前摇、挥击 Active 和收招在敌人 sprite 上一眼可辨，命中窗口与 Active 视觉一致。
6. Grab 前摇持续移动、Active 冲刺和空冲 OffBalance 与正式帧衔接自然，不回归 Week27 节奏。
7. Grab 抱住后立即切换 Bite 动画，伤害/控制仍只发生一次。
8. 三敌人场景只有主攻者播放攻击动作；Support/Search/Unaware 不错误播放攻击 sheet。
9. 左右朝向使用正确行，不镜像错位、不漂脚、不裁切头部或脚部。
10. 临时移除/破坏攻击 sheet 后程序仍可启动并使用既有 fallback，不出现透明敌人。
11. 鼠标射击、后坐力、子弹拖尾、命中火花、背包、柜体、撤离、死亡/超时结算和跨 Raid 流程不回归。
12. 全流程无 Runtime Library / `gtest_ar_`；多人战斗中反馈不闪烁成持续满屏效果。

人工项目在用户实际执行前均保持“未验证”。正式资产的“视觉通过”必须由 art-control 审核，不能由生产任务自评替代。

## 风险、失败语义与回滚

- 风险：独立动画时钟会与攻击命中窗口漂移。采样只读取 EnemyAttackState 的 phase/remaining/config，不保存自己的 progress。
- 风险：美术任务延迟阻塞整周。代码与 fallback 可先验证，但 Week29 不能把 fallback 称为“正式动画完成”；资产未批准时状态保持部分完成。
- 风险：三张 sheet 身份或 pivot 不一致。合同冻结完整帧 canvas、footline、pivot、行语义和默认敌人身份，art-control 逐张审核 contact sheet 与叠帧预览。
- 风险：反馈层复制伤害/命中事实。GameplayWorld 只在现有命令成功点发出短时视觉事件；反馈过期不影响 gameplay state。
- 风险：满屏多反馈降低可读性。计时使用刷新/max 而非无界累加，强度 clamp，敌人命中确认使用单个短状态。
- 回滚：攻击 sheet 加载和反馈状态可作为 Week29 单片回退；Week26–28 gameplay 状态、资产与 manifest 不迁移、不覆盖。

## 进度记录

- 2026-08-09：Week28 PR #52 以 merge commit `c4658e7` 合入；读取 art pipeline、Art Bible、manifest、生产协议和 Character Layer calibration contract，确认正式敌人攻击帧属于 Week29 显式重开范围且必须使用独立生产任务。

## 决策日志

- 2026-08-09：攻击动画只采样既有 EnemyAttackState，不增加第二时钟；逻辑阶段仍是命中和动作的唯一事实来源。
- 2026-08-09：正式包采用三张独立 6 帧×2 行 sheet，复用现有 256×320 frame、pivot、footline 与 64×80 runtime draw contract。
- 2026-08-09：本轮增加枪口、命中确认和玩家受伤脉冲，不引入相机系统、屏幕震动或全局 hit-stop，避免把表现周扩大为世界坐标重构。
- 2026-08-09：未获用户明确允许创建独立生产任务前，不由 art-control 当前任务直接生成图像；先推进可测试代码合同和 production brief。

## 最终结果、验证与偏差

尚未实现。下一安全步骤是在 Week29 功能分支先实现 SDL 无关的攻击表现采样和 CombatFeedbackState 测试，同时编写 `enemy_default_attacks_v1` 的 production brief 与 JSON contract；正式图像生成等待独立用户可见生产任务。

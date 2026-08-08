# Project Raidline 路线图

本文件区分“已完成事实”和“产品候选”。未来阶段不是自动承诺；每次实施前重新核对用户优先级、仓库状态和美术依赖。

## 已进入 main 的里程碑

| 阶段 | 已闭环结果 |
| --- | --- |
| Week 1–3 | CMake/vcpkg/GTest/CI、SDL App、输入、玩家移动、资源加载与基础渲染 |
| Week 4–8 | Projectile、Enemy、AABB、世界边界、朝向射击与 cooldown |
| Week 9–13 | 命中结果、Texture RAII、动画、ParticleSystem、Health、Damage、Death、Score |
| Week 14–16 | ItemDefinition/ItemInstance、GroundItem、最近拾取、GridInventory、事务式放置/移动与交互状态 |
| Week 17 | 鼠标 hover/选择/阈值拖拽、多格 grab offset、合法性反馈与 Tab/Esc 帧级取消仲裁 |
| 背包 UX 稳定化 | 平滑像素虚影与独立吸附候选；PR #32 |
| Week 18 | 世界柜体与双容器指定格转移、贴右丢弃条、角色脚下落点、纯鼠标背包；PR #33 |
| Week 19 | 整栈快捷转移、拖拽旋转、9mm 堆叠、数量拖拽与正式弹药资源；PR #34 |
| Week 20 | 一次性柜体搜索、加权 Loot、可注入随机源、临时 Inventory 原子提交；PR #35 |
| Week 21 | 六态 RaidSession、固定撤离点、连续撤离、终止竞态与终局冻结；PR #36 |
| Week 22 | 撤离存入内存 Stash、死亡/超时损失、Blocked 原子失败、结算统计与反馈；PR #40 |
| Week 23 | 可重复 Raid 会话、跨局 Stash、稳定 ID 高水位、只读仓库与空背包重开；PR #42 |
| Week 24 | 玩家 3 HP、敌人接触伤害、真实死亡出口与成功/失败完整垂直回归；PR #44 |
| Week 25 | MainMenu/Base/Raid/RaidResult 顶层流程、非 Raid 冻结、单地图部署与跨局返回；PR #46 |

详细历史保留在 `doc/DevLog_Week*.md` 与已完成 ExecPlan；其中分支和 CI 描述只代表当时快照。

## 当前开发：Week26 鼠标瞄准、射击与 V0 后坐力

Week25 已通过 PR #46 合入 `main@08e4475`。下一步让鼠标世界位置独立决定瞄准方向，以左键连续射击，并建立可调、确定性且可自动测试的扩散累积/恢复与 V0 可视后坐力；Space 在本轮保留为回归路径。Week26 不实现弹药消耗、换弹、武器系统、相机震动、音效、敌人攻击或 AI。活动计划见 `doc/exec-plans/active/week26-mouse-aim-shooting-recoil.md`。

## Week25–Week30 推荐顺序

| 阶段 | 玩家可感知结果 | 关键工程主题与边界 |
| --- | --- | --- |
| Week25 | 主菜单进入基地，从基地部署到当前地图；结算后回到基地并可再次出战 | 小型 `GameFlow` 状态机、屏幕级输入/渲染路由、单一地图副本生命周期；不做最终美术或通用 SceneManager |
| Week26 | 鼠标决定瞄准方向，左键射击；射击具备可调后坐力、扩散和恢复手感 | 屏幕/世界坐标、Aim 输入、武器射击状态、确定性随机/曲线与调试参数；保留键盘回归路径直到验收 |
| Week27 | 敌人拥有三类可读攻击：抓是短距离突进，挠是近距离普通攻击，咬是有明显前摇的高伤害控制攻击 | `Windup/Active/Recovery` 动作阶段、单次命中窗口、位移提交、控制状态与动画事件；先用占位表现验证规则 |
| Week28 | 敌人能感知、追击、保持攻击距离并按条件选择抓/挠/咬 | 可测试 AI 状态机、感知/失去目标、转向与局部 steering、攻击选择和 cooldown；暂不引入导航网格或行为树框架 |
| Week29 | 玩家射击与敌人攻击形成可玩的战斗节奏，抓/挠/咬具有正式且可读的动画表现 | 命中反馈、后坐力调参、受伤反馈、难度与完整成功/失败回归；按独立美术生产协议制作并接入三类攻击动画，音效仍另行排期 |
| Week30 | 基地可选择多个固定地图副本，不同地图拥有独立出生、敌人、Loot 与撤离配置 | 显式 MapDefinition/MapId、确定性实例创建和数据边界；不做程序生成地图 |

上述顺序按依赖排列：顶层流程先于地图扩展，玩家战斗入口先于敌人战斗平衡，敌人动作规则先于 AI 选择，最后再集中做手感与内容扩展。每周实施前仍需把具体数值、美术依赖和人工验收冻结到独立 ExecPlan。

## 独立稳定化候选

- 角色上/下移动动画和停止朝向表现。
- [库存拖拽可行位置原子交换（#38）](https://github.com/Disthinker/Project-Raidline/issues/38)，包括一个拖拽物与目标处若干 placement 的事务式重排。
- [Ctrl/Shift 数量点击锁定拖拽（#39）](https://github.com/Disthinker/Project-Raidline/issues/39)，松开鼠标和修饰键后虚像继续跟随，下一次点击提交。
- 把共享业务源码抽为核心 library，降低重复编译与旧对象风险。
- 把 Phase 1 资源测试接入 CTest/CI，并继续加固批准资产的不可覆盖保护。
- 在不扩大玩法范围的前提下拆分 `App` 的背包 UI 编排职责。

## 未排期边界

程序生成地图、ECS、通用 SceneManager、导航网格/行为树框架、网络同步、大规模 GameplayWorld 重写、复杂装备/武器改装、音频和完整商业化内容均未排期。任何新任务如需引入其中一项，先建立独立范围和 ExecPlan。

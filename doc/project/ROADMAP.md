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
| Week 26 | 鼠标独立瞄准、左键/Space 统一射击、扩散/后坐力、1200 px/s 高速弹丸与火光命中反馈；PR #48 |
| Week 27 | 近距默认挠击、条件 Grab→Bite、空冲 `OffBalance`、三级移动速度与双方受击顿挫；PR #50 |

详细历史保留在 `doc/DevLog_Week*.md` 与已完成 ExecPlan；其中分支和 CI 描述只代表当时快照。

## 当前开发：Week29 战斗反馈与抓/挠/咬动画

Week28 冻结提交 `07755f6` 已通过本地 544/544、真实窗口 1–13 和精确 head Ubuntu/Windows CI，并由 PR #52 合入 `main@c4658e7`。Week29 将增加 SDL 无关的攻击表现采样、正式敌人攻击 sheet 接线、命中确认/受伤脉冲/枪口反馈和一轮战斗节奏调参；正式帧资产按 `enemy_default_attacks_v1` 独立生产包生成、审核后才能发布，音效、完整相机系统、血液与尸体仍不在本轮。活动计划见 `doc/exec-plans/active/week29-combat-feedback-and-attack-animation.md`。

## Week25–Week30 推荐顺序

| 阶段 | 玩家可感知结果 | 关键工程主题与边界 |
| --- | --- | --- |
| Week25 | 主菜单进入基地，从基地部署到当前地图；结算后回到基地并可再次出战 | 小型 `GameFlow` 状态机、屏幕级输入/渲染路由、单一地图副本生命周期；不做最终美术或通用 SceneManager |
| Week26 | 鼠标决定瞄准方向，左键射击；射击具备可调后坐力、扩散和恢复手感 | 屏幕/世界坐标、Aim 输入、武器射击状态、确定性随机/曲线与调试参数；保留键盘回归路径直到验收 |
| Week27 | 敌人低速二维追击并以短前摇挠击为常态；玩家可通过中距离保持诱发抱咬，空冲会倒伏失衡，双方受击均有顿挫 | 确定性条件选招、Grab→Bite 原子动作链、`OffBalance`、静止/常态/攻击三级速度、受击时间倍率与代码占位表现 |
| Week28 | 敌人对玩家的感知与战术选择更可信，多个敌人不再只做全知直线追击 | 感知/失去目标、距离保持、转向与局部 steering、多敌人协作、攻击选择调优；暂不引入导航网格或行为树框架 |
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

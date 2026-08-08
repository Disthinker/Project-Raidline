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

详细历史保留在 `doc/DevLog_Week*.md` 与已完成 ExecPlan；其中分支和 CI 描述只代表当时快照。

## 当前开发：Week23 待规划

Week22 已通过 PR #40、Windows/Ubuntu CI 和真实窗口 1–8 合入 `main`。下一阶段候选是“可重复 Raid 会话与 Stash 边界”；实施前需要冻结 GameSession 所有权、跨 Raid 稳定 ID、结算后重开和最小 Stash 查看/出战规则，并建立独立 ExecPlan。

## 推荐后续候选

| 候选阶段 | 产品结果 | 关键工程主题 |
| --- | --- | --- |
| Week23 | 可重复 Raid 会话与 Stash 边界 | GameSession 所有权、跨 Raid 稳定 ID 分配、结算后重开、最小出战/仓库查看入口 |
| Week24 | 垂直切片 V0 收口 | 进入→战斗/避敌→搜索→背包→撤离/死亡→结算→重开完整回归 |
| 后续 V1 | 装备与内容扩展 | 装备栏、WeaponInstance/装填、重量/耐久、尸体搜索、AI、固定地图、数据驱动与持久化 |

## 独立稳定化候选

- 角色上/下移动动画和停止朝向表现。
- [库存拖拽可行位置原子交换（#38）](https://github.com/Disthinker/Project-Raidline/issues/38)，包括一个拖拽物与目标处若干 placement 的事务式重排。
- [Ctrl/Shift 数量点击锁定拖拽（#39）](https://github.com/Disthinker/Project-Raidline/issues/39)，松开鼠标和修饰键后虚像继续跟随，下一次点击提交。
- 把共享业务源码抽为核心 library，降低重复编译与旧对象风险。
- 把 Phase 1 资源测试接入 CTest/CI，并继续加固批准资产的不可覆盖保护。
- 在不扩大玩法范围的前提下拆分 `App` 的背包 UI 编排职责。

## 未排期边界

程序地图、ECS、SceneManager、网络同步、大规模 GameplayWorld 重写、复杂装备/武器改装、音频和完整商业化内容均未排期。任何新任务如需引入其中一项，先建立独立范围和 ExecPlan。

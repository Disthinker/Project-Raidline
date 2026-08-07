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

详细历史保留在 `doc/DevLog_Week*.md` 与已完成 ExecPlan；其中分支和 CI 描述只代表当时快照。

## 当前开发：Week20 最小可搜索柜体与 Loot

`codex/week20-searchable-containers` 已完成本地逻辑实现与自动验证：

- 柜体具有显式 Unsearched/Searched 状态，取空不刷新；
- 首次范围内 F 搜索后打开，后续 F 只打开现有库存；
- 默认 3 次加权 Loot 抽取与 Ammo9mm 10–30 数量范围；
- 可注入随机源、稳定权重/数量边界测试；
- 临时 GridInventory 完整生成后原子提交；
- 最终 placement 才分配世界稳定 ID，失败不推进序列。

Windows Debug 全目标构建、受影响程序直跑 74/74、全量 CTest 386/386 与真实窗口 1–9 项均已通过；当前只待冻结提交的单一 PR/CI 与合入。

## 推荐后续候选

| 候选阶段 | 产品结果 | 关键工程主题 |
| --- | --- | --- |
| Week20 收口 | 最小可搜索柜体与 Loot 进入 `main` | 人工验收、所有权审查、文档、单一 PR/CI |
| Week21 | 最小 RaidSession 与撤离点 | Preparing/InRaid/Extracting/Extracted/PlayerDead/RaidEnded、计时与离开取消 |
| Week22 | 结算与最小 Stash | 撤离保留、死亡丢失本局所得、局外仓库、重开 |
| Week23 | 垂直切片 V0 | 进入→战斗/避敌→搜索→背包→撤离/死亡→结算→重开 |
| 后续 V1 | 装备与内容扩展 | 装备栏、WeaponInstance/装填、重量/耐久、尸体搜索、AI、固定地图、数据驱动与持久化 |

## 独立稳定化候选

- 角色上/下移动动画和停止朝向表现。
- 把共享业务源码抽为核心 library，降低重复编译与旧对象风险。
- 把 Phase 1 资源测试接入 CTest/CI，并继续加固批准资产的不可覆盖保护。
- 在不扩大玩法范围的前提下拆分 `App` 的背包 UI 编排职责。

## 未排期边界

程序地图、ECS、SceneManager、网络同步、大规模 GameplayWorld 重写、复杂装备/武器改装、音频和完整商业化内容均未排期。任何新任务如需引入其中一项，先建立独立范围和 ExecPlan。

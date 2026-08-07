# Project Raidline 路线图

本文件区分“已完成事实”和“产品候选”。未来周次是用户给定的推荐路线，不是已承诺范围；实施前必须重新核对产品优先级与仓库现状。

## 已完成里程碑

| 周次 | 已进入 `main` 的闭环 |
| --- | --- |
| Week 1–3 | CMake/vcpkg/GTest/CI、SDL App 壳、输入、玩家移动、资源加载与基础渲染 |
| Week 4–5 | Projectile、射击输入、AABB、Enemy 和最小命中循环 |
| Week 6–8 | GameplayWorld 边界、移动敌人、朝向射击与 cooldown |
| Week 9–12 | 命中结果、Texture RAII、通用动画、ParticleSystem |
| Week 13 | Health、Damage、Death、Score |
| Week 14 | ItemDefinition/ItemInstance、GroundItem、最近物品拾取 |
| Week 15 | GridInventory、row-major first-fit、事务式放置、只读背包 UI |
| Week 16 | `canMove`/`tryMove`、设备无关键盘交互状态、合法性预览与提交/取消 |
| Week 17 | 鼠标 hover/选择/阈值拖拽、多格 grab offset、合法性反馈、帧级 Tab/Esc 取消仲裁 |
| 背包 UX 稳定化 | 平滑像素拖拽虚像，保留独立吸附格候选；PR #32 已合入 `main` |

详细演进保留在 `doc/DevLog_Week*.md`，但其中的分支和 CI 状态是历史快照。

## 最近完成：Week 17 鼠标格子背包交互

目标是保留 Week 16 键盘行为，同时增加可测试的坐标转换、独立 `hoveredCell`、点击选择、拖拽阈值、多格 grab offset、`canMove` 预览、`tryMove` 释放提交、网格外规则、取消、SDL 适配和代码绘制反馈。

实现、专用测试和帧级输入仲裁已经接入；29/29 鼠标测试、295/295 CTest、Windows/Ubuntu CI 以及 2026-08-07 的 9/9 真实窗口验收共同完成闭环。详细证据见 [已完成 ExecPlan](../exec-plans/completed/week17-mouse-inventory-interaction.md) 和 [问题台账](KNOWN_ISSUES.md)。

## 当前开发：Week 18 双容器转移、丢弃与纯鼠标背包

`codex/week18-inventory-transfer-drop` 已在本地实现玩家 10×6 背包与世界柜体所拥有的 6×6 `GridInventory` 之间的指定格/首个可用位置安全转移、贴右侧丢弃条、角色脚下落点，以及纯鼠标交互。Tab 只显示玩家背包，靠近柜体按 F 才显示右侧容器；方向键和两种 Enter 的库存语义、黄色键盘焦点与 Idle 选择框已删除。Windows Debug 完整构建和全量 CTest 304/304 已通过；修订版第 12–16 项真实窗口验收、提交 CI 与合入仍待完成，因此 Week18 尚未列入已完成里程碑。

## 推荐后续候选

| 候选阶段 | 产品结果 | 关键工程主题 |
| --- | --- | --- |
| Week 18 收口 | 双容器转移、显式丢弃与纯鼠标背包 | 真实窗口验收、Windows/Ubuntu CI、合入 `main` |
| Week 19 | 一个可搜索地图容器与最小 Loot | 容器交互生命周期、权重 Loot、可注入随机数、确定性测试 |
| Week 20 | 最小 RaidSession 与撤离点 | Preparing/InRaid/Extracting/Extracted/PlayerDead/RaidEnded、计时与离开取消 |
| Week 21 | 结算与最小 Stash | 撤离保留、死亡丢失本局所得、局外仓库、重开 |
| Week 22 | 垂直切片 V0 | 进入→战斗/避敌→搜索→背包→撤离/死亡→结算→重开 |
| Week 23–30 | V1 候选 | 装备栏、WeaponInstance/弹药、尸体搜索、Enemy 状态机、固定地图/建筑、数据驱动物品/Loot、持久化、回归与性能 |

## 未排期边界

物品旋转、快捷转移、上下文菜单、tooltip、堆叠、重量、耐久、复杂装备、程序地图、ECS、SceneManager 和大规模 GameplayWorld 重构均未排期。任何任务如需引入其中一项，先更新范围和 ExecPlan，不把它夹带进相邻功能。

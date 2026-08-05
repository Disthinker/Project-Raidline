# Project Raidline 路线图

本文件区分“已完成事实”“当前计划”和“产品候选”。除 Week 17 的活动计划外，未来周次是用户给定的推荐路线，不是已承诺范围；实施前必须重新核对产品优先级与仓库现状。

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

详细演进保留在 `doc/DevLog_Week*.md`，但其中的分支和 CI 状态是历史快照。

## 当前：Week 17 鼠标格子背包交互

目标是保留 Week 16 键盘行为，同时增加可测试的坐标转换、独立 `hoveredCell`、点击选择、拖拽阈值、多格 grab offset、`canMove` 预览、`tryMove` 释放提交、网格外规则、取消、SDL 适配和代码绘制反馈。

当前本地实现、专用测试和帧级输入仲裁已经接入；由于精确提交的 CI 与 9 项真实窗口验收尚未全部完成，仍不能标记为完成。以 [活动 ExecPlan](../exec-plans/active/week17-mouse-inventory-interaction.md) 和 [问题台账](KNOWN_ISSUES.md) 为实施事实来源。

## 推荐后续候选

| 候选阶段 | 产品结果 | 关键工程主题 |
| --- | --- | --- |
| Week 18 | 玩家背包与第二个 GridInventory 安全转移 | move-only 跨所有者转移、稳定 ID、失败回滚、自动/指定位置 |
| Week 19 | 一个可搜索地图容器与最小 Loot | 双容器 UI、权重 Loot、可注入随机数、确定性测试 |
| Week 20 | 最小 RaidSession 与撤离点 | Preparing/InRaid/Extracting/Extracted/PlayerDead/RaidEnded、计时与离开取消 |
| Week 21 | 结算与最小 Stash | 撤离保留、死亡丢失本局所得、局外仓库、重开 |
| Week 22 | 垂直切片 V0 | 进入→战斗/避敌→搜索→背包→撤离/死亡→结算→重开 |
| Week 23–30 | V1 候选 | 装备栏、WeaponInstance/弹药、尸体搜索、Enemy 状态机、固定地图/建筑、数据驱动物品/Loot、持久化、回归与性能 |

## 未排期边界

物品旋转、快捷转移、上下文菜单、tooltip、堆叠、重量、耐久、复杂装备、程序地图、ECS、SceneManager 和大规模 GameplayWorld 重构均未排期。任何任务如需引入其中一项，先更新范围和 ExecPlan，不把它夹带进相邻功能。

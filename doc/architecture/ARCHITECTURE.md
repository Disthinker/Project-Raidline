# Project Raidline 目标架构

最后核对：2026-08-14。本文描述 Core Extraction Alpha 的目标边界以及 V0 的迁移方式；实际完成度以 `doc/project/CURRENT_STATE.md` 和测试为准。

## 组合根与场景关系

```text
App
└─ GameFlow
   └─ GameSession
      ├─ ProfileState
      ├─ BaseWorld      (仅 Base 状态存在)
      ├─ GameplayWorld  (仅 Raid 状态存在)
      ├─ ActionDomain
      └─ SettlementDomain
```

- `App` 负责 SDL 生命周期、输入翻译和只读投影渲染。它不提交跨所有者状态拼装，不根据表现猜测领域结果。
- `GameFlow` 只仲裁 `MainMenu → Base → Raid → RaidResult → Base`。
- `GameSession` 是进程内组合根，持有长期档案与当前唯一场景运行时。Base 和 Raid 不同时推进。
- `BaseWorld` 只拥有玩家位置、碰撞、设施交互范围、稳定 FacilityId 和短期交互上下文；Base 不运行敌人、Loot、撤离计时或战斗。
- `GameplayWorld` 只拥有本次 Raid 的空间实体、AI、动作推进和 `RaidSessionSnapshot`；它不拥有长期 Stash 或货币真值。

## 长期档案与持久化

`ProfileState` 持有：

- 唯一 `AssetRegistry`；
- Stash、三槽 Equipment、容器关系与 AssetLocation；
- 普通货币、救济批次和轻量引导标志；
- 稳定 ID 高水位；
- 存档版本、档案 ID、pending Raid 和最后已结算 ID。

Persistence 保存值与关系，不保存指针、引用、vector 下标、UI 格位对象或场景地址。每次 Base 资产事务和 Raid 结算成功后执行临时文件写入、校验和原子替换；安全备份仅在主档损坏或迁移失败时恢复。

## 定义与实例

- ContentDefinition 是不可变注册数据：物品、容器、槽位、武器、弹药、弹匣、医疗、地图、Loot 和敌人部署使用稳定定义 ID。
- AssetInstance 是唯一资产：稳定实例 ID、定义 ID、数量/次数及当前启用字段。
- 未启用的耐久、故障、复杂伤势等不创建空运行状态；未来通过版本化字段和能力注册扩展。
- 显示名称、图标、动画、颜色和文件路径不参与领域分支。

## 领域边界

| 领域 | 权威职责 | 对 App 输出 |
| --- | --- | --- |
| AssetRegistry | 唯一实例所有权、ID 分配和位置关系 | 只读资产投影 |
| Inventory/Equipment | 原子移动、交换、堆叠、装备、容器规则 | 查询计划、提交结果、拒绝原因 |
| WeaponAmmo | 弹匣有序序列、枪膛、Base 压卸弹、Raid 换弹和击发消耗 | 弹药/动作/击发结果 |
| Action | 开始、阶段、提交点、中断、完成及暂持资产 | 动作投影与结果 |
| RaidSession/Map | MapDefinition、本局种子、出生撤离配对、Loot/敌人快照和生命周期 | 本局只读投影、撤离资格 |
| Settlement | 成功带回、失败全损、容量阻塞、结算幂等 | RaidResult 数据 |
| Economy/Relief | 固定供应、回收、货币、防套利和单份救济 | 交易/资格/余额结果 |
| Persistence | 版本化保存、迁移、校验、备份和 pending Raid 恢复 | 加载/保存/恢复结果 |

## 射击迁移边界

正式方向是不创建可渲染/可碰撞场景实体弹丸，而是保存短生命逻辑飞行记录并分段连续扫掠。Alpha 不同时重做动态准星、部位、穿透和最终弹道。

当前迁移合同：

```text
WeaponFire/Ammo
  -> ShotCommand
  -> ShotResolution
  -> temporary V0 flight adapter
  -> HitResult
  -> damage / feedback / App projection
```

`Projectile` 只能作为临时飞行适配器。WeaponAmmo、伤害、持久化和 App 不得要求该类型。普通命中、爆头和弱点只能来自未来扩展的 HitResult，App 不按碰撞标签猜测。

## 事务、ID 与幂等

- 查询不修改；命令在提交前复核版本和全部前置条件。
- 一次事务涉及的源、目标、父容器、装备槽、货币和 ID 分配要么全部成功，要么全部不变。
- 每个唯一资产任一时刻恰有一个 AssetLocation。动作暂持和结算中转也是显式位置。
- 资产 ID 单调递增且不复用；定义 ID 稳定；Raid/结算使用独立稳定 ID。
- Deploy 先原子持久化资产快照、结算 ID 和 pending 标记，再进入 Raid。终局消费同一 ID；重放返回已提交结果。

## 迁移策略

按垂直切片建立新边界，再迁移消费者；不做一次性重写：

1. Slice 0 建立治理、射击窄边界和 Timeout 退场测试。
2. Slice 1 把长期资产移出场景并建立 Base/Persistence。
3. Slice 2 把武器弹药、100 HP 和医疗接入真实资产。
4. Slice 3 把固定常量迁入 MapDefinition/RaidSnapshot 并替换 Timeout。
5. Slice 4 接入经济、救济和连续多局产品门槛。

不得借迁移引入大型 ECS、服务定位器、万能事件总线或没有消费者的未来状态。

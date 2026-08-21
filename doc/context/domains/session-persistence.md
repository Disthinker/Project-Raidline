# 领域上下文：会话、生命周期与持久化

## 权威与入口

- `GameSession` 组合一个长期 `ProfileState` 与互斥的 Base/Raid 活动运行时。
- 主要入口：`src/game_session.h`、`src/raid_lifecycle.h`、`src/raid_settlement.h`、`src/save_repository.h`。
- SaveRepository 保存稳定 ID、定义 ID、关系、版本、高水位和幂等键；不保存指针、UI 格位或场景地址。

## 稳定执行路径

- Base 事务：复制候选 Profile -> 查询/执行 -> 验证 -> 原子保存 -> 交换内存状态。
- Deploy：先原子保存精确出击前 Profile，再建立仅内存的 Raid。
- 终局：以唯一 SettlementId 原子结算；重复提交不重复改变长期状态。
- 异常退出：重新加载出击前 Profile；当前 Alpha 不续玩中途 Raid。

## 核心不变量

- 同一结算最多改变档案一次；失败保存不提交内存候选。
- schema 逐版本迁移；未知版本、坏引用、重复 ID、高水位倒退拒绝加载。
- 主档/备份有校验与原子替换；备份不是玩家回档槽。
- Deploy、死亡、撤离、主动退出和旧 pending rollback 必须收束所有资产位置。

## 主要消费者与测试

- 消费者：GameFlow、Base/Raid runtime、Inventory/Combat/Economy、SDL client。
- CTest：`ctest -L area-lifecycle`、`ctest -L area-persistence`、`ctest -L layer-integration`、`ctest -L sentinel`。
- 关键测试：`test_raid_lifecycle.cpp`、`test_save_repository.cpp`、`test_alpha_extraction_session.cpp`、`test_alpha_hardening.cpp`。

## 跨域风险与禁止事项

这是最高风险邻接领域。改变 Profile/AssetRecord/Location、schema、Content 兼容、Deploy 或 Settlement 必须使用 `authority` 风险门并运行迁移、长序列、Sentinel、Integration 与全量回归。不得为了方便在 Raid 帧内持续写主档。

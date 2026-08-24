# Raid 主动高危与高级资源区 v1 ExecPlan

## 产品结果与范围

每张现有固定地图新增一个代码表现的高危控制地标。玩家在常规阶段进入地标并持续按住交互键 4 秒，可以主动、不可逆地提前进入与自然超时完全相同的持续高危。松开交互、离开地标、受到伤害、被 Grab/OffBalance 控制、打开模态界面或 Raid 终结都会清空本次进度。

每张图新增一个高级资源区和两个高级 Loot 插槽。插槽内容在 Deploy 时使用独立稳定随机流一次生成并写入 `PendingRaidSnapshot`，常规阶段不可见也不可拾取；进入高危后只解除访问限制，不重新生成、不重抽、不改变资产 ID。当前仅复用已发布定义和代码 fallback，不恢复正式美术生产。

本切片不实现随机危机池、停电、火灾、道路封锁、地图情报、条件/凭证撤离、随机地图、新敌人、新物品、尸体搜索、寻回或新美术音频。

## 基线与依赖

- 分支：`codex/raid-high-risk-control-resource-v1`，基线 `origin/main@773443b`。
- PR #75 已通过 CI、用户正常游玩验收并普通合入，提供 `Regular → HighRisk`、信号撤离和有界压力。
- 外部 GDD 只读；冻结事实是每图一个可中断控制地标、主动与自然高危共用状态、高级 Loot 开局确定且高危只负责开放。
- 高级资源点最终数量、正式内容品质和情报可见性仍未冻结；v1 的两个插槽与现有 Loot 表是可回滚玩法基线，不代表最终内容量。

## 权威状态与数据合同

- `RaidSession::triggerHighRisk()` 是唯一主动阶段切换入口；只接受活动中的 Regular Raid，重复或终局调用零修改。
- `MapDefinition::highRisk` 拥有控制地标、交互时长、高级资源区、冻结插槽及 Loot 表引用；ContentRegistry 拒绝越界、重复 ID、缺失引用和非法几何。
- `GameplayWorld` 拥有单局控制交互进度，并只投影地标、进度和开放状态。App 只绘制，不改变阶段或 Loot 资格。
- `RaidLootSnapshot::requiresHighRisk` 冻结访问资格。`GameSession` 同时负责附近拾取与 App 可见性查询，避免客户端猜测。
- 普通 Loot 继续使用原随机流和 6～9 插槽；高级 Loot 使用独立命名 PCG32 流，新增内容不能改变同 seed 的普通 Loot 选择。

## 实施与回滚边界

1. 扩展内容 schema、三张地图配置、验证器与 content version 兼容路径。
2. 扩展 PendingRaidSnapshot、保存往返、指纹与 Profile 校验；旧 `raid-pressure-1` pending Raid 继续可恢复。
3. 实现 RaidSession 主动切换、GameplayWorld 按住交互/中断状态机和输入门控。
4. 在 Deploy 中冻结高级 Loot，在 GameSession 中统一拦截常规阶段访问。
5. 接入控制点、资源区、进度和锁定/开放的代码表现与 HUD。
6. focused tests、Windows Debug 全目标、完整 CTest 与 exact-head Windows/Ubuntu CI 通过后交给用户正常游玩验收。

回滚使用普通 revert，不改写历史。旧 content v10/schema v6 存档兼容路径保留。

## 自动化与人工门槛

- RaidSession：主动触发只成功一次；与自然超时使用相同阶段、撤离和压力合同；非法调用零修改。
- ContentRegistry：三图具有唯一控制点和高级资源区；越界、障碍重叠、非法时长、重复插槽和缺失 Loot 表均拒绝。
- RaidLifecycle/Profile/Save：普通 Loot 同 seed 不变；高级 Loot 开局冻结；v10 兼容；schema v6 往返保留锁定字段；失败不复制资产。
- GameplayWorld/GameSession：按住完成、松开/离区/受伤/控制中断；常规阶段不可见不可拾取；自然或主动高危后可拾取；重复交互不重抽。
- 人工验收：任选地图主动拉闸并确认 4 秒可中断读条；高危开启后取得高级资源；第二局等待自然高危并确认同一区域同样开放；确认普通 Loot、信号撤离、压力与结算无回归。

## 进度

- [x] 2026-08-24：PR #75 合入后核对主线、GDD、内容、RaidSession、Loot 快照与输入边界，冻结 v1 范围。
- [x] 2026-08-24：完成 content v11、三图控制/资源配置、独立高级 Loot 流、快照访问标记、schema v6 往返与 v10 兼容。
- [x] 2026-08-24：完成按住/中断交互、主动阶段切换、常规阶段访问门控与代码 fallback 表现。
- [x] 2026-08-24：修复首轮验收发现的三项阻断：Raid 中 Profile revision 变化导致拖拽失效；普通撤离/信号撤离/控制点/资源区几何重叠；碰撞回滚双轴导致沿墙移动被锁死。
- [x] 2026-08-24：增加当前状态重验、关键区域互斥校验和逐轴碰撞滑动测试；Windows Debug 全目标构建、focused 175/175 与完整 CTest 817/817 通过，开发代理未启动游戏。
- [x] 2026-08-24：PR #76 exact-head Windows/Ubuntu CI 完成。
- [x] 2026-08-24：用户正常游玩复验通过，PR #76 以 merge commit `bc26337` 普通合入 main。

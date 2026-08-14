# RL-INV-003：弹药整堆拖放合并

## Status

Complete — latest-main local/CI revalidation and manual acceptance passed; PR #54 merged as `5bbddc3`

## Baseline

- Branch: `codex/rl-inv-003-ammo-stack-merge`
- Original base: `origin/main@61718f6`
- Revalidation base: `origin/main@c7a3931` after accepted PR #55
- Scope: only RL-INV-003 inventory stack placement behavior and its documentation/tests

## Goal

普通鼠标拖拽一整堆物品到同定义、未满的可堆叠物品上时，使用统一的领域事务完成合并。`Ammo9mm` 的单堆上限保持 60；目标堆容量不足时，目标补满，源堆余量保留在原位置。

## Current Behavior and Root Cause

`InventoryInteractionState` 对普通拖拽生成不带 `selectedQuantity` 的 `InventoryPlacementRequest`。App 对这种请求只调用 `GridInventory::tryTransform` 或 `tryTransferItemTransform`，两者都把已占用目标格判定为非法，因此普通整堆拖拽无法合并。Ctrl/Shift 数量拖拽走 `tryPlaceItemQuantityAt`，但该 API 承诺“精确请求数量或完全不变”，不能用放宽其原子语义的方式修复本缺陷。

## Contract

- 空目标格：保持现有同容器 transform / 跨容器 transfer 行为，包括请求的朝向。
- 匹配且未满的目标堆：尽可能合并，最多达到定义中的 `maxStackSize`。
- 源堆可被完全吸收时，移除源放置；目标稳定 ID、位置和朝向不变。
- 目标容量不足时，目标补满；源堆以原稳定 ID、原位置和原朝向保留余量。
- 合并不创建稳定 ID，也不消耗世界 ID。
- 满堆、定义不匹配、不可堆叠物品、缺失源、跨容器稳定 ID 冲突均失败且不改变任何状态。
- 普通拖拽的预览查询与释放提交使用同一组领域规则。
- Ctrl/Shift 的精确数量选择语义保持不变。

## Non-goals

- 不处理 RL-INV-001、RL-INV-002。
- 不重构输入状态机或 GameplayWorld 数量分拆协议。
- 不包含 Week29 战斗反馈、攻击动画或任何美术资源生产。
- 不改变弹药定义和 60 的单堆上限。

## Implementation Plan

1. 在 `inventory_transfer` 增加普通整堆指针放置的无副作用查询和事务命令。
2. 先覆盖同容器/跨容器、完全吸收/溢出余量、失败不变及查询不变的回归测试。
3. 将 App 普通拖拽的预览和提交统一接入该领域 API；数量拖拽继续使用原 API。
4. 运行聚焦测试、Windows Debug 完整构建和全量 CTest。
5. 更新架构、不变量、已知问题、当前状态、学习记录和中文交接文档。

## Verification

### Automated

- `InventoryTransferTest` / `InventoryWholeStackPlacementTest`
- `MouseInventoryInteractionTest` / `MouseInventoryIntegrationTest`
- Windows Debug full build
- Full CTest suite

### Manual acceptance

Result: 2026-08-11，用户确认以下 1–5 项真实 Windows Debug 窗口验收全部通过。

1. 同一背包中，把一堆 9mm 弹药拖到同定义未满堆上，预览为合法且释放后合并。
2. 跨背包/外部容器重复上述操作。
3. 目标剩余容量小于源数量时，目标变为 60，源位置保留正确余量。
4. 目标已满或定义不匹配时，预览非法且释放后两边不变。
5. Ctrl/Shift 数量拖拽仍严格执行所选数量，不发生隐式部分提交。

## Risks and Mitigations

- 风险：同容器删除源放置后使目标指针失效。缓解：事务只跨删除保存稳定 ID 和数值，删除后重新按 ID 查找。
- 风险：预览与提交规则漂移。缓解：两者调用同一领域查询/命令，并为查询无副作用补测试。
- 风险：意外改变精确数量拖拽。缓解：不修改其合同，并保留现有数量测试。

## Progress

- [x] 从最新 `origin/main` 创建独立分支并确认工作区干净。
- [x] 定位普通整堆拖拽绕过合并事务的根因。
- [x] 加入回归测试，并确认实现前因缺少整堆领域 API 编译失败。
- [x] 实现领域查询/命令并接入 App 预览与提交。
- [x] 完成本地自动化验证：聚焦 45/45、Windows Debug 226 步全目标重建、全量 CTest 552/552。
- [x] 完成 diff、安全与文档复核，准备人工验收清单。
- [x] 用户完成真实窗口同/跨容器、溢出、失败场景和精确数量回归验收（2026-08-11，1–5 全部通过）。
- [x] 以 merge commit 吸收 `origin/main@c7a3931`，保留新版产品架构并只带回 RL-INV-003 的窄库存不变量。
- [x] 完成组合基线本地复验：重新配置、55 步增量全目标构建、精确新增行为 8/8、广义库存/鼠标 37/37、全量 CTest 558/558。
- [x] 精确 merge head `0523b3d` 的范围检测、Windows 和 Ubuntu CI 全部通过。
- [x] PR #54 已于 2026-08-14 接受并以 merge commit `5bbddc3` 合入 main；RL-INV-003 关闭并归档本计划。

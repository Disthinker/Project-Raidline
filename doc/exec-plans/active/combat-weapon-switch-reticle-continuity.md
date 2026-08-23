# 武器切换准星连续性缺陷修复 ExecPlan

## 玩家结果与范围

玩家在 Raid 中通过 `1`/`2`/`3` 完成限时武器切换时，准星保持在切换前的实际世界位置，不再跳回系统指针锚点或另一处默认位置。新武器仍使用自己的射速、散布、伤害、射程与弹道配置。

本修复只处理准星状态连续性。它不改变武器切换时长、输入键位、弹药/耐久/故障、散布模型、后坐力参数、命中解析、音画资源或存档格式。

## 基线、所有权与根因

- 分支：`codex/fix-weapon-switch-reticle-continuity`，基线 `origin/main@a32c476`。
- PR #73 已进入主线；本缺陷与地图内容无数据依赖。
- `GameplayWorld::WeaponAimState` 唯一拥有当前准星位置、相对输入锚点、控制速度和有界后坐力运动。
- 根因是切换完成后，武器重新配置同时重建了 `WeaponAimState`。相对鼠标模式下，实际准星可能已离开固定系统指针锚点；重建后的下一帧会从该锚点重新初始化，形成可见瞬移。

## 修复合同

- 武器重新配置始终原位更新 `WeaponAimState` 的武器参数，保留其空间与输入连续性。
- 普通武器切换仍重建 `WeaponFireState`，不得把上一把武器的射击冷却或动态散布瞬态带给下一把武器。
- F10 当前武器调参继续原位重配置两类瞬态，不改变既有会话级调参合同。
- 修复不增加 Profile 字段、稳定 ID、事务或持久化内容。

## 验证与交付

- simulation 回归：相对鼠标已使实际准星离开系统指针锚点后，更换武器配置保持准星位置；静止输入下一帧仍不跳变。
- simulation 隔离：重新配置后新武器可以立即按自身冷却击发，证明旧武器射击冷却未泄漏。
- session 回归：真实定时切换从主武器到手枪后保持准星位置。
- 运行 GameplayWorld 与 AlphaExtractionSession focused tests、Windows Debug 全目标增量构建和完整 CTest；推送后要求 exact-head Windows/Ubuntu CI。
- 自动化和 CI 完成后由用户正常游玩验收：在 Raid 中把准星移离窗口中心，连续切换主武器/手枪，确认切换完成前后准星没有瞬移，随后两把武器均可正常瞄准和开火。

回滚方式为普通 revert 本缺陷提交；不改写历史、不删除分支。

## 进度

- [x] 2026-08-23：从最新 `origin/main@a32c476` 建立独立缺陷分支并定位状态重建根因。
- [x] 2026-08-23：完成准星连续性修复和 simulation/session 双层回归。
- [x] 2026-08-23：Windows Debug 全目标重建、GameplayWorld/AlphaExtractionSession 99/99 与完整 CTest 795/795 通过。
- [ ] exact-head Windows/Ubuntu CI 完成。
- [ ] 用户正常游玩验收完成。

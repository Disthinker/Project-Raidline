# Project Raidline ExecPlan 规范

ExecPlan 是可执行、可验证、可回滚的产品切片合同。新计划以玩家结果或领域迁移命名，不再以 Week 编号命名；旧 Week 计划保留为历史证据。

每份活动计划必须包含：

1. 产品结果、唯一范围来源和明确排除。
2. 当前 Git/PR/构建基线与依赖关系。
3. V0 差距及可复用、需重构、需新建、停止扩展项。
4. 状态所有权、领域命令/结果、稳定 ID 和持久化影响。
5. 实施步骤与每步退出条件。
6. 自动化门槛、真实窗口验收和证据格式。
7. 提交/PR 边界、风险、回滚和未解决问题。
8. 进度记录；只有证据完成后才能勾选。

活动计划应能让接管者不依赖聊天历史继续工作。普通实现细节由开发主控决定；只有产品支柱、失败损失、商业模式、叙事主方向或显著范围扩张才升级给用户。

当前总计划：`active/core-extraction-alpha.md`。

当前实现计划：`active/regional-map-intelligence-v1.md`。Persistent Base、Extraction Loop、Alpha Hardening、Survival Loadout、Combat、Raid Pressure 与 Base Growth PR #78～#91 已接受；旧活动文档保留交付证据。

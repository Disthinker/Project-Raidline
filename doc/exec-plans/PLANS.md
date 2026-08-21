# Project Raidline 任务合同与 ExecPlan

## 两种文件各自负责什么

- `*.task.toml` 是短 Task Envelope：允许改哪里、保护什么、风险、影响和证据门。每个 tracked change 都需要；模板见 `TASK_ENVELOPE_TEMPLATE.toml`。
- ExecPlan 是多步骤执行与接管合同。`cross-domain`、`authority`、迁移和玩家可见垂直切片必须有；简单 local/domain 修复不为形式重复写长计划。

Task Envelope 不是第二份 ExecPlan。若实施必须离开 allowed paths、进入 protected domain 或触碰 authority path，先显式扩大 envelope 和证据门，再继续编辑。

## ExecPlan 最小内容

1. 工程/玩家结果、范围来源和排除。
2. 接受基线、依赖与真实差距。
3. 所有权、命令/结果、稳定 ID、持久化/Content 影响。
4. 实施步骤及可观察退出条件。
5. 风险决定的 area/Sentinel/Integration/migration/long-sequence/full/CI/人工门。
6. 提交与回滚、未解决风险、证据进度。

活动计划只放 `active/`；接受或取消后移到 `completed/`。暂停中的命名工作可以保留 active，但必须在文档首部明确暂停原因。临时分支、CI 和 PR 状态只写活动计划/PR，不复制到稳定项目文档。

## 新任务入口

```powershell
python tools/raidline_governance.py preflight --task "<request>"
# 建立 task envelope / 必要时 ExecPlan 后：
python tools/raidline_governance.py preflight --task "<request>" --envelope doc/exec-plans/active/<task>.task.toml --run-sentinel
```

普通实现细节由开发主控决定。只有产品支柱、失败损失、商业模式、叙事方向或显著范围扩张才升级给用户。

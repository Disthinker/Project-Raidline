# Project Raidline 渐进式上下文入口

本目录不是第二份架构百科。它只负责把一次任务路由到最小必要上下文；稳定细节仍以源码、测试、CMake 和架构不变量为准。

## 四层加载模型

1. **全局层**：始终读取根 `AGENTS.md`，运行 `python tools/raidline_governance.py preflight --task "<任务>"`，查看自动生成的快照与路由。
2. **领域层**：只读取路由返回的一个主领域上下文；只有实际依赖、改动路径或失败证据证明需要时，才读取相邻领域。
3. **任务层**：读取当前 `*.task.toml` 和对应 ExecPlan。任务边界短，ExecPlan 只用于多步骤、高风险或玩家可见切片。
4. **历史层**：仅在追溯决策、迁移来源或回归时读取 completed ExecPlan、Week 文档、DevLog 和 Git 历史；普通任务不默认加载。

快速入口：

```powershell
python tools/raidline_governance.py route --task "修复 Raid 换弹中断"
python tools/raidline_governance.py snapshot
python tools/raidline_governance.py preflight --task "修复 Raid 换弹中断" --envelope doc/exec-plans/active/<task>.task.toml --run-sentinel
```

## 路由后的读取顺序

1. 主领域上下文及其列出的权威头文件和测试。
2. 通过 `rg` 做符号、include、调用点和测试消费者搜索。
3. 读取任务实际触及的实现文件。
4. 只有出现跨域调用、持久化字段、Content 引用或所有权迁移时，才加载相邻领域。

若上下文扩大，任务记录必须写明原因，例如：“新增读取 `session-persistence`，因为 AssetRecord 字段进入 schema v6 序列化”。不得为了保险默认读取全部项目状态、路线、DevLog 或所有活动计划。

## 领域索引

| 领域 | 上下文 | 典型入口 |
| --- | --- | --- |
| 资产与库存 | `domains/asset-inventory.md` | Profile、AssetRegistry、格子、装备、弹药资产 |
| 战斗 | `domains/combat.md` | 射击、命中、AI、伤害、医疗动作 |
| 会话与持久化 | `domains/session-persistence.md` | GameSession、Deploy、Settlement、存档与迁移 |
| 内容 | `domains/content.md` | ContentRegistry、JSON 定义、兼容版本 |
| 客户端表现 | `domains/client-presentation.md` | SDL 输入、App、UI、渲染与音频 |
| 构建与交付 | `domains/build-delivery.md` | CMake、CTest、CI、PR 与治理工具 |

机器路由规则位于 `doc/context/context-map.json`。修改规则时必须运行治理工具测试，并确保领域上下文仍短且不复制长期架构全文。

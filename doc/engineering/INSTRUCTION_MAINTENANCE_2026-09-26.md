# 指令维护记录 · 2026-09-26

一次性维护记录，不是新的常驻工作流。未改业务代码、模型、权限、依赖或美术；未提交、推送、切分支或合并。

## 本地事实与加载边界

- 当前会话目录 `C:\Users\Administrator\Documents\美术资源生成` 是无 HEAD 的辅助 Git 目录，含既有未跟踪工作；真正游戏仓库为 `E:\WorkPlace\Projects\C\Project Raidline`。本次从实际仓库检查，而非按旧交接覆盖。
- 已 fetch；分支 `codex/hospital-district-layout-v1`，HEAD `4bbc0c4e67e9e7856408bee3c0725603c39c4f55`，origin/main `6eae1830e1ff8fe046d4141cd2b844cdda968d62`；PR #164 为 Draft。游戏工作树开始时干净，结束时仅本次指令维护未提交。
- CLI `0.155.0-alpha.16.4`；app-server 实测 Codex Home 为 `C:\Users\Administrator\.codex`。共享 config 默认 `gpt-6-astra` / `ultra`；项目角色未指定独立模型，不据此断言每个实际子任务都运行同一模型。
- 常驻：游戏根 AGENTS.md；辅助目录新增定位 AGENTS.md。未发现适用上级非空 AGENTS/override、项目子目录 override 或配置自定义 fallback。两目录不会自动互相继承项目技能。
- 按需：游戏 `.agents/skills/` 六技能及 `.codex/agents/` 角色。app-server `skills/list(forceReload=true)` 实测六项均 enabled、无错误、无同名重复；辅助目录无这些项目技能。
- 实测 `config/read(includeLayers=true)`：游戏项目 config/hooks/exec-policy 层因未列为 trusted 被跳过；共享用户层生效。AGENTS/技能仍可发现。未修改信任设置；角色 TOML 的实际 spawn/模型继承未验证，不能从 config 的 agents=null 推断全部自定义角色均不可用。
- 只读检查了用户 config、空用户 AGENTS、用户 skills/rules、插件元数据及系统配置来源；未修改共享或第三方文件。项目没有 config.toml、额外 hooks/rules 或技能生成器；Git pre-commit 来自现有 Python pre-commit 配置，不生成这些指令。
- 检查隐藏/忽略项与相关链接：未发现本项目指令符号链接或重复安装。现有 governance `__pycache__` 无对应源入口，不作为生效规则。美术模板、生产协议、脚本入口、教学模板仅按需引用；未重写。未扫描全盘或读取凭据。

## 实际修改

- `AGENTS.md`：去掉已被后续产品阶段取代的 Alpha 全局禁令；保留平台、权威来源、资产/敌人稳定 ID、事务、保存/结算、射击结果边界、人工验收和美术暂停。明确共享机制与活动 Policy，避免复用旧 Base 无战斗假设。减少每次任务全量读文档/fetch/规划的要求，不降低代码发布门槛。
- 六个 `SKILL.md`：描述改为具体触发条件；重复发布要求引用根规则。构建只读相关设置，文档维护不跑 C++ 全套；验收只准备用户正常游玩流程，不自行开游戏。架构技能保留跨 Daily/Defense/Raid 生命周期合同测试。
- 验收和美术两个 `agents/openai.yaml`：纠正默认提示与技能权限不一致；未改变隐式调用策略。
- 三个 `.codex/agents/` 文件（implementer/reviewer/verifier）：保留有明确职责的实现、独立复核和验证；减少全资料、全流程默认负担。explorer 已简洁，未改。四角色原 sandbox/model 设置保持不变。
- `raidline-learning-analyst.toml` 移至 `doc/handoffs/legacy-agents/raidline-learning-analyst.toml.example`，保留教学能力参考，但不再自动发现；原教学模板保留。没有建立强制多 agent 编排。
- `doc/exec-plans/PLANS.md`：保留可接管合同，V0 差距和保存迁移按影响选用；多份互相覆盖的阶段记录移到 `history/PLANS_THROUGH_2026-09.md`，不默认加载。当前进度仍由 CURRENT_STATE/ROADMAP/活动计划维护。
- 新增美术 `references/production.md`：原有角色分权、逐候选调用、资产身份、64 像素规格、透明/派生 QA、工具覆盖风险均保留，只在获授权的生产任务读取；并非把全部旧负担搬到必读附件。
- 辅助目录新增定位 AGENTS.md，防止在那里误认游戏状态或重启已移交美术主控的 catalog 批次。

## 验证与未验证

已执行通过：六技能官方 quick_validate、YAML/TOML 解析和必需字段、UI 元数据与 skill 名称一致、四角色原模型/权限不变、引用文件和 Markdown 链接、git diff --check。新旧 HEAD 相同。

运行时实测：`codex debug prompt-input` 前后对比显示游戏根指令实际加载、技能描述实际进入发现上下文；`app-server` 的 initialize/config/read/skills/list 使用本机生成的协议 schema，无模型 turn、无改配置调用。默认上下文不包含美术生产附件和教学档案。显式技能文本的 debug prompt 也只是初始上下文，不证明模型随后已读取 skill 正文。

代表场景：文档拼写小修→无专项/只检查文本；头文件修改后 gtest_ar_→构建技能/依赖重建；SDL 拖放高亮→C++ 验证、不是美术生产。三场景的上下文组装均实际执行；路由和完成条件是人工推演，未做模型行为 A/B、实际子代理 spawn 或游戏验收。没有性能或全模型兼容性结论。本次无需 CTest/CI，也未启动游戏。

唯一配置遗留：若需确认项目角色实际可用，应在后续正常项目会话检验；是否将 E 盘游戏项目标记可信由用户决定，不能为通过本次检查擅自放宽。现有对话可能保留旧指令，建议从游戏仓库开新会话，技能列表若缓存则重新加载/重启。

## 规模与回滚

以下为 UTF-8 解码并统一换行后的字符数，不是 token 或费用测量；不同任务只会加载其中一部分。

| 类别 | 前 | 后 |
| --- | ---: | ---: |
| 游戏常驻 AGENTS | 8,221 | 5,546 |
| 六技能发现描述 | 975 | 737 |
| 六技能按需入口（含描述） | 12,998 | 8,439 |
| 角色定义（5→4） | 4,497 | 3,097 |
| 按需 ExecPlan 指南 | 2,274 | 669 |
| 仅生产时读取的美术附件 | 0 | 4,881 |
| 非自动加载历史/教学档案 | 0 | 3,616 |
| 辅助目录定位说明 | 0 | 673 |

备份和原始验证输出：`C:\Users\Administrator\Documents\美术资源生成\tmp\instruction-maintenance-20260926`。before.json/逐文件原始字节保留本次开始状态；rollback.json 记录维护后哈希。回滚命令：

```powershell
python "C:\Users\Administrator\Documents\美术资源生成\tmp\instruction-maintenance-20260926\maintenance.py" restore
```

恢复器先检查所有本次修改文件是否又被编辑；有后续变化会拒绝，不覆盖新工作。只恢复本次文件，不运行 git reset、不清理其他未跟踪内容。备份不在 agent/skill 扫描目录；请在需要回滚期间保留。

## 参考依据

本次采用官方建议中的明确触发、按需披露和与风险相称的约束，不机械追求短文件或批量替换模型名：[GPT‑6 Astra 技能/提示建议](https://learn.chatgpt.com/blog/rethinking-skills-and-prompts-for-gpt-6-astra)、[AGENTS](https://learn.chatgpt.com/docs/agent-configuration/agents-md)、[Skills](https://learn.chatgpt.com/docs/build-skills)、[Subagents](https://learn.chatgpt.com/docs/agent-configuration/subagents)、[配置参考](https://learn.chatgpt.com/docs/config-file/config-reference)。官方发现说明与本机实测分开记录，未宣称未执行的动态行为已验证。

# Raidline 三线程入口 · RL-COLLAB-20261011-v1

2026-10-11 原生 `list_threads` 实测：以下均为已有独立持久 Codex 会话，host `local`，共同 cwd `C:\Users\Administrator\Documents\美术资源生成`。同 cwd 不代表同角色；四个 `.codex/agents/*.toml` 只是按需临时工，不能继承这些会话历史。

| 角色 / 原标题 | threadId | 启动后按需角色正文 |
| --- | --- | --- |
| 主控线程 | `01a0ddf7-3cbe-7403-8f5b-92e2a0cab9ad` | [main-controller](roles/main-controller.md) |
| 策划线程 | `019feff4-477c-75c2-bf31-3f09ff1e3891` | [planner](roles/planner.md) |
| 美术主控线程 | `019f7e6c-12be-7a22-8d03-490e83dc8236` | [art-controller](roles/art-controller.md) |

游戏目录 `E:\WorkPlace\Projects\C\Project Raidline`，当前只有一个 Git worktree。产品资料库 `E:\WorkPlace\Projects\C\Project Raidline GDD` 保持只读。美术 catalog `D:\Raidline_Art\Catalog\comshadow` 属美术主控；任何支持目录旧补丁均不是游戏部署状态。启动/恢复先读游戏根 AGENTS、CURRENT_STATE 和本角色正文；只按当前任务读取专项技能。未知会话身份先核对入口，不能因身处该 cwd 就执行游戏开发。

## 实际通信（Codex desktop 原生工具）

1. 用 `mcp__codex_app__list_threads` 核对 ID/host/cwd/status。只复用上述目标；标题不提供授权。若线程已在工作，用同一个原生入口排队/协调到安全边界，不另起 CLI resume、第二入口或同名线程。
2. 主控调用 `mcp__codex_app__send_message_to_thread`，保持目标 model/thinking 不传覆盖。此调用可触发目标会话；本项目用户已在 2026-10-11 维护请求中明确授权日常三线程协调。消息是主控请求，不是人类批准。新的无关收件人仍需明确授权。
3. 用 `mcp__codex_app__wait_threads` 获取对应目标的结果；带上返回 cursor 继续等待，单次等待不超过 60 秒。忙碌目标可在安全边界用带四元组的 commentary 返回独立协调结果，不必结束其原授权任务；commentary 不唤醒等待，但超时快照含进度，需精确原文时 `read_thread` 定向取该 message/turn。不要高频反复读历史。
4. 主控验证当前待收请求与返回的四元组（request_id、task_id、version、baseline），以及实际 target ID。过期/错对象/错版本/不完整回复保留为历史，不替换当前计划；必要时请求纠正。工具的发送回执、idle、交接文件或一句“已收到”不代表工作结果。
5. 主控读取真实回复后更新原任务记录中的协调证据、范围与状态。不要修改 Codex 会话库或日志。空闲目标在原请求 turn 最终回复，忙碌目标按上款回复；两者都不反向发送启动主控第二轮。需要主动回传时必须另有明确的人类授权。

请求最小格式（不转发全部历史）：

```text
主控协调请求
request_id: <唯一 ID>
task_id: <当前任务>
version: <当前方案/协议版本>
baseline: <commit/tree 或明确的未提交候选>
requested_output: <一个具体结果>
facts_and_refs: <最新交付事实、问题、必要绝对路径>
authorization: <已获批范围；本次允许读/写什么；缺失的批准>
reply: 原 turn 回复包含上述四元组、role/status、结果与限制；忙碌时可 commentary
```

每个任务在已有 ExecPlan 中记录待收 request ID 和有效回复；不建立新的流程数据库。没有可用 native 工具时如实记录“通信受阻”；文件可以保存证据，但不能声称已唤醒或自行编造策划任务。未接入的 CLI 不具备这里的 desktop 线程通信；不要把这些工具名包装成可执行 shell 命令。

## 按需入口与加载边界

- 主控选题、状态恢复、交付：`.agents/skills/raidline-project-governor/SKILL.md`。
- 开发计划/执行、Bug 方案/修复、报告：`.agents/skills/raidline-feature-slice-delivery/SKILL.md`。
- 用户正常游玩清单/反馈：`.agents/skills/raidline-gameplay-acceptance/SKILL.md`。
- 美术查询/获批包交付：`.agents/skills/raidline-art-pipeline/SKILL.md`；没有资产需求不调用美术走形式。
- 架构、构建技能只在涉及相应合同/验证时读取。无固定多轮角色审查链。

这份 map/角色正文是现有会话的可重读启动材料，并非自动发现的 custom agent 注册。支持目录 AGENTS 是自动入口，显式路由到游戏根/角色；E 盘项目 skills 可由游戏 cwd 发现，支持 cwd 不继承它们。用户全局配置、信任及模型保持原样；已打开的会话须实际重读新正文，不能声称磁盘更新已清除旧上下文。当前加载与真实通信证据见 [维护报告](../engineering/COLLABORATION_MAINTENANCE_2026-10-11.md)。

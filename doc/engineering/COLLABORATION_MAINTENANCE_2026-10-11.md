# 三线程协作配置维护 · RL-COLLAB-20261011-v1

本次用户明确授权实际配置优化与无副作用核验，未授权游戏开发、正式资产生产或其他在制任务合并。维护完成后先待用户验收。本记录只陈述实测范围，不把提示规则视为绝对技术强制。

## 核查到的真实部署

主控、策划、美术是三个既存独立 Codex 持久会话，ID/cwd/host/原标题由原生 list_threads 与定向 read_thread 核对，见 [THREADS](../coordination/THREADS.md)。三者当前均从支持目录启动；游戏唯一 worktree 在 E 盘。支持目录 Git 无 HEAD，已有未跟踪补丁、工具、输出保留。游戏分支 `codex/pointer-aim-continuity-v1` / HEAD `d391278a844f45c41cd624bdd4a17f797fc832f9`；旧维护与未验收鼠标修复均未提交，不在本次交付范围。

已核查两个 cwd 的根 AGENTS、适用祖先/override、用户空 AGENTS、用户 config/rules/技能入口、游戏隐藏/未跟踪 `.agents`/`.codex`、六技能及 UI YAML、四临时角色 TOML、09-26 维护记录、PLANS/完成定义、活动状态与美术协议/生产附件及原脚本入口。未发现生效子目录 override、项目 config.toml 或指令生成源；Python pre-commit 检查不生成这些指令。旧 learning-analyst 停用文件和历史索引保持原样，不重新执行旧维护。外部 GDD 不写入。

本机 CLI/app-server `0.162.0-alpha.17.2`。用户配置默认 `gpt-6.1-sol/ultra`；定向读取三个既存会话近期 turn_context 所见主控/美术 `gpt-6.1-sol/xhigh`，策划 `gpt-6-astra/medium`，仅代表当时记录，并非所有未来 turn 模型；没有覆盖模型。原生 send/wait/read 工具当前可用。共享信任、sandbox、工具权限、模型/插件/automation 均未修改。

美术当前另有获用户授权恢复的 comshadow 整理：已有 heartbeat `comshadow` 指向原美术线程，配置实测 `ACTIVE`。旧 10-07 PAUSED 不能覆盖最新授权；catalog 整理仍不授予正式游戏包生产/发布。未改该自动任务或 catalog；该独立线程自身授权的进行中工作不计成本次维护动作。

## 常驻及按需改动

| 入口 | 本次变化 |
| --- | --- |
| 游戏与支持目录 AGENTS | 三角色和人类 plan/Bug/acceptance/Git 门槛常驻；支持 cwd 明确重读 E 盘规则；修正 catalog 实际路径，不依据旧交接调度 |
| governor / feature / gameplay-acceptance / art 四现有技能 | 选题实际通信/状态恢复/已验收交付；真实文件计划与所有 Bug 先审批；冻结候选/正常操作复验；包授权与单写者交接 |
| governor / feature / art 三个 UI YAML | 默认提示与新流程一致；隐式调用策略及依赖字段保留 |
| 四现有临时角色 TOML | 明确批准来源/任务版本和临时角色边界；原 sandbox/model 字段不变，无固定多轮审查链 |
| 新 THREADS 与三份角色正文 | 保留既存 ID/cwd/资料范围，映射真实 native transport；并非创建新持久会话或自动注册同名 agent |
| 一个 feature references/reports.md | 合并开发计划、完成/复验、Bug 方案的短格式，按需读取 |
| PLANS / 完成定义 / 美术协议与生产附件 | 状态/版本/真实人类来源；配置验证与 C++ 门槛区分；验收后约定交付；临时分工与正式目录单写者，无擅建长期任务 |
| CURRENT_STATE / 本维护活动计划 | 维护独立待验收入口，鼠标修复仍待原人工验收；原历史正文保留 |

没有新增 skill、模型、工具链、通用平台或固定审批角色。架构/构建技能内容及领域/保存/性能/画面规格保留。备份、一次性检查脚本、prompt/通信证据均存支持目录 tmp，未留在 agent/skill 扫描路径。

## 验证证据与边界

实际通过：六技能官方 quick_validate、六 UI YAML 和四角色 TOML 解析及策略/模型/权限字段保留、35 个本地 Markdown 引用、git diff --check。第一次引用检查发现三个技能的相对路径多跳一级，已修正；最终检查通过。架构/Validation 常驻合同与原 CURRENT_STATE 历史正文逐字节/文本核对保留。

真实通信两条均通过，不是手动复制结果：主控原生发送→原目标 turn 实际读取正文→带请求/任务/版本/基线的回复→主控读取原文并检查四元组及 target。策划请求 `RL-CFG-20261011-PLANNER-01` 在 turn `01a12aba-162c-76f0-99a5-c3d428da0c4c` 最终回复；美术请求 `RL-CFG-20261011-ART-01` 在原进行中 turn `01a12a97-f810-71c2-980e-0bf784d5b321` 安全复核边界用 commentary 回复，未暂停原用户授权任务。两者均确认完整读了五份指定正文，美术还重读了忙碌返回约定；均没有反向发送启动主控第二轮或写游戏/Git/manifest。返回所见模型区别与原只读/生产权限如上。`communication-raw.json` 保存实际回复与定向读取命令，`communication-checks.json` 记录目标/四元组验证；未由检查脚本发出线程消息或制造回复。

独立临时 subagent `/root/approval_forward_test` 只读新主控规则/三个流程技能，对隔离 fixture 实际进行 15 个模型决策测试（14 案例，其中首验收/重复验收分开）：准备下一项只报计划、改计划不执行、明确批准后执行、Bug 先调查方案、拒绝后不委派执行、自动化/“继续”不代验收、当前验收立即约定本地交付且不重复问、重复验收幂等、旧请求/旧批准不放行、变更候选需新验证/验收、恢复待批、清晰改后开始无需再问、CI 失败保留已验收交付受阻、暂停隔离、保存/正式美术扩张重新审批。主控逐项核对结果符合要求，摘要见 `behavior-checks.json`。这是一次实际模型无副作用场景测试，并非真实代码修复/真实 Git 执行，也不保证全部模型或未来 turn 遵守；模拟批准从未写入真实任务状态。

一次性备份/回滚与回复验证辅助脚本在可丢弃临时 fixture 中实跑 **5/5 单元测试**：恢复维护前脏字节且保留其他未跟踪文件；后续编辑、损坏备份和越界路径均在写入前拒绝；错误 request/task/version/baseline/role/target 被拒绝。真实项目没有运行 restore。最终保护核对：原游戏代码/CMake/鼠标测试及计划、其他在制/旧维护文件、用户 config/rules、旧支持补丁均未被本次修改；HEAD/branch/index 保持开始值。共 25 个配置/资料路径发生本次变化，以最终 manifest 为准；当前候选版本 `RL-COLLAB-20261011-v1`，待人工验收。

已实际执行本机 `codex debug prompt-input` 两 cwd 的上下文组装，以及只读 app-server `initialize/config/read/skills/list(forceReload=true)`，没有 model turn、thread/create/resume 或配置写 API。两 cwd 均加载新常驻版本；E 盘发现六个 enabled 项目技能且零错误/无重复，支持 cwd 不继承它们；报告格式附件未进入常驻上下文。主控已实际读取新根/角色和相关技能。

仍有本机限制：游戏目录未列 trusted，app-server 回报项目 config/hooks/exec-policy 层被跳过；本次不修改信任。六技能/AGENTS 实测可发现不代表四自定义角色真实 spawn 已验证，TOML 解析/字段保留与运行时使用分开。三长期线程的通信采用 desktop 原生能力，不依赖这些自定义角色或 shell 伪接口；无该工具的 CLI 环境应报通信受阻。既有会话磁盘更新不会清除旧上下文，因此针对原 ID 发送重读并核对实际读取/回复。

官方依据：[AGENTS 加载](https://learn.chatgpt.com/docs/agent-configuration/agents-md)、[技能按需加载](https://learn.chatgpt.com/docs/build-skills)、[自定义临时 agents](https://learn.chatgpt.com/docs/agent-configuration/subagents)、[配置参考](https://learn.chatgpt.com/docs/config-file/config-reference)。本次依此保持常驻门槛、专项技能/附件按需；实际字段/发现/通信结论以上述本机结果为准，不由文档推断通信成功。

## 回滚与验收

原始字节/开始 dirty 状态在 `C:\Users\Administrator\Documents\美术资源生成\tmp\collaboration-maintenance-20261011\before.json` 和 `before/`；维护候选精确集合由 `rollback.json` 的 changed path/after_sha256 标识。回滚命令（不执行即不产生动作）：

```powershell
& 'E:\WorkPlace\Projects\C\Project Raidline\.venv\Scripts\python.exe' -B -X utf8 'C:\Users\Administrator\Documents\美术资源生成\tmp\collaboration-maintenance-20261011\maintenance.py' restore
```

恢复前逐文件核对维护后哈希，有后续编辑则拒绝覆盖；只还原本次配置，不 reset/clean、不删其他成果。会话上下文的回滚需在同一 native 入口要求原三个角色重读恢复后的正文；文件恢复不自动撤销已送达指令。没有修改旧 startup 日志/会话数据库。现有三个会话本轮均已实际加载，无需新建/替换；后续恢复按支持 AGENTS 重读角色/状态，若 app 技能列表缓存旧描述则重新加载/重启，不能假定旧长上下文已消失。

人工配置验收：

1. 从本报告→THREADS→各角色/根规则：三个真实 ID、原只读/包授权边界和职责符合预期。
2. 从 PLANS→短格式：可判断具体文件/行为、批准版本、候选与正常验收步骤；每个用户 Bug 必须先方案批准。
3. 查看隔离行为证据：继续/改计划/拒绝修复/测试成功不能放行，当前候选明确验收触发约定交付，旧版本/候选变化与恢复等待被拦下。这里的模拟批准不作用于真实游戏任务。
4. 查看加载、真实发送/回复和保护证据：模板按需、三角色实际重读；游戏代码/未验收成果和全局权限没有被本次修改。

当前无游戏实机验收、真实 Git 执行、远端 CI 或正式资产产出；配置维护不需要无关全量游戏测试。本轮尚未 commit/push/merge，鼠标修复没有新增批准/验收。当前只待用户对本配置候选验收；若未来要验证/启用被跳过的项目配置层，是否设置信任由用户另行决定，本次不需要也不请求放宽。本报告提交后等待验收，不选下一游戏任务。

## 2026-10-11 验收与配置交付

以上是配置验收前证据。用户随后明确："本次配置维护验收通过，请完成提交与合并，不要夹带尚未验收的鼠标修复，暂不启动下一项开发。"。已验收候选的全部 25 个路径哈希复核一致，配置规则不再改动；仅追加真实验收/交付状态。

采用隔离配置分支从准确 origin/main 建立，排除 src、tests、CMake 和鼠标活动计划；CURRENT_STATE 只接本次配置前缀与主线历史，不带原工作区鼠标实现记录。为使干净主线得到实际已验收的部署，原字节携带已有两技能、验收 UI YAML、PLANS 历史引用、已停用角色归档及旧维护记录；不重新执行旧维护，也不纳入其他历史任务改动。支持工作区 AGENTS 作为 `doc/coordination/support-workspace-AGENTS.md` 原字节留档，实际 cwd 配置不搬迁。

此次沿用已确认的 origin/PR 普通 merge 策略，必要 CI 不绕过，不新开下一开发任务。精确交付标识以 Git/PR 及支持目录交付凭据为准，避免为复制 CI 数值再建提交。

# 三线程协作配置维护 v1

- task_id：`collaboration-maintenance-v1`
- state：用户已验收（配置集合；不含鼠标修复）。交付状态由真实 Git/PR 判定：本配置交付提交合入并在 `main` 可达则已交付，否则交付中；必要检查失败则已验收交付受阻。恢复先核对包含关系，避免重复创建提交/PR/merge。
- plan_version：`RL-COLLAB-20261011-v1`
- baseline：`codex/pointer-aim-continuity-v1` / HEAD `d391278a844f45c41cd624bdd4a17f797fc832f9`；保留旧维护和鼠标修复未提交成果，未选择新开发基线或切换分支。
- approval_source：2026-10-11 主控线程中用户附文 `C:\Users\Administrator\.codex\attachments\c1ceccad-1d34-4bf9-ab75-480575deaa17\已粘贴的文本.txt` 明确要求“实际修改、验证并汇报”，并授权项目协作配置、角色、技能、必要模板/轻量脚本。该直达维护指示授权本方案范围实施；没有给游戏/资产新任务或 Git 交付授权。
- candidate_id：验证完成后以备份目录 rollback.json 的精确 changed_paths/after_sha256 识别本配置集合；不是当前基线 HEAD 本身。报告和状态记录纳入集合。
- accepted_candidate_id：`RL-COLLAB-20261011-v1` / manifest `9d299132fd5c069347b2bd62b5e36483ed36c44d148f79fb1c519ef8d78e798a`。
- acceptance_source：2026-10-11 主控线程真实用户指示："本次配置维护验收通过，请完成提交与合并，不要夹带尚未验收的鼠标修复，暂不启动下一项开发。"。授权配置提交与合并，不授权鼠标交付或下一项开发。隔离 fixture 从未作为授权源。

## 目标与范围

将真实已有三个长期会话接到“策划选题→主控具体计划→用户批准→实施/集成/验证→用户验收→约定交付”的流程；所有用户 Bug 先诊断方案审批。常驻门槛短，按需技能/报告格式有唯一入口，线程通信实测带版本相关性。现有模型、沙箱/工具权限、外部 GDD 只读与游戏领域合同保留。

维护游戏/支持目录 AGENTS、现有 governor/feature/acceptance/art 技能和四临时角色，新增线程 map、三份持久角色正文、一个合并格式附件；更新 PLANS、完成定义与美术协议的冲突条款。辅助备份/检查仅在支持目录 tmp 下，不在自动加载入口。架构/构建技能内容无须再改；不修改业务/测试代码，不启动游戏，不生产资产，不重做 09-26 维护，不合并鼠标修复或其他在制任务。

## 保护与验证

开始状态已原字节备份至 `C:\Users\Administrator\Documents\美术资源生成\tmp\collaboration-maintenance-20261011`：before.json 与 before/。仅改允许清单；结束复核非本次脏文件、HEAD/branch/index、用户级配置/规则/旧支持补丁不变。报告会保存格式/引用、本机 CLI prompt/app-server skills 加载证据，以及两个真实通信请求/结果和隔离行为测试；这些不是游戏验收或绝对工具强制保证。

协调均完成，pending_request：无。`RL-CFG-20261011-PLANNER-01` → 策划 `019feff4-477c-75c2-bf31-3f09ff1e3891` 的 turn `01a12aba-162c-76f0-99a5-c3d428da0c4c` 最终回复；`RL-CFG-20261011-ART-01` → 美术 `019f7e6c-12be-7a22-8d03-490e83dc8236` 的在制 turn `01a12a97-f810-71c2-980e-0bf784d5b321` 安全边界 commentary 回执。均实际重读五份正文且 task/version/baseline 与 target 匹配；美术未停止或扩展原 catalog 授权工作。原文及验证存备份目录 communication-raw/checks.json。

verification：六技能 metadata/官方 quick_validate、六 YAML/四 TOML 与模型/权限字段、35 本地引用、whitespace/历史正文保护通过；本机 prompt/app-server skills 加载实测通过；三角色实际使用新正文、两条 native 通信通过；15 个独立模型无副作用审批场景符合要求；一次性辅助脚本隔离测试 5/5。代码/CMake/鼠标候选文件、HEAD/branch/index 及共享配置保持原样。无游戏/全量 CTest/远端 CI/实际 Git，custom-agent spawn 未验证。完整证据与边界见 [维护报告](../../engineering/COLLABORATION_MAINTENANCE_2026-10-11.md)。

回滚只恢复本次文件到维护开始的字节，不使用 git reset/clean；恢复器先对比维护后哈希，发现后续改动即拒绝。撤销已送达会话上下文还需通过相同原生入口要求目标重读恢复后的文件，不改内部会话数据。

Git：用户现已明确验收并授权提交/合并；采用本项目已用的 origin `Disthinker/Project-Raidline` → PR → 普通 merge 至 main 流程，不强推。以隔离分支交付配置；CURRENT_STATE 只加入配置状态，游戏/鼠标实现和其活动计划不入包。补齐当前已验收部署依赖的两技能、验收 YAML、历史索引/停用归档与旧维护记录，支持 cwd AGENTS 原字节作为 repo 文档留档；所有依赖均与维护开始字节一致，无新增功能行为。原游戏工作区不切分支、不暂存。提交/PR/merge 精确标识由交付凭据及原生 Git/PR 记录提供。

## 配置验收入口

用户可查阅根/支持 AGENTS、THREADS/角色、按需格式和最终维护报告；验证“准备下一项/改计划/反馈 Bug/拒绝方案”均停在正确待批状态，自动化不代验收，当前候选明确验收后按约定交付且不再重复问同一合并授权。本轮已验收，只完成约定配置交付，不发下一游戏任务。

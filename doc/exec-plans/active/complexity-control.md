# Codex 开发治理与复杂度控制 ExecPlan

状态：本地实施与验证完成，待 exact-head CI

基线：`origin/main@ba3375e`；独立分支 `codex/complexity-control`

任务边界：`doc/exec-plans/active/complexity-control.task.toml`

## 1. 工程结果与排除

建立一套可由新 Codex 会话直接运行的最小机制：先路由到必要领域，再按任务风险选择上下文和证据；用 CTest、架构守卫和影响面检查阻止跨域破坏。聊天历史不再是接管前提。

本任务不改变玩法、生产源码、Content、Save schema、资产所有权、美术或 manifest；不合并或复制 Draft PR #69。

## 2. 审计事实

- 审计起点：`codex/combat-direct-aim-spread-tracer-v2@c89f5ae`，工作区干净；Draft PR #69 OPEN/CLEAN，exact-head 范围、Windows、Ubuntu CI 均已通过，但用户正常游玩验收尚未完成。
- 接受基线：`origin/main@ba3375e`，Content `combat-input-content-7`，Save schema v6，四个生产库、50 个非 main production cpp。
- Accepted main 重新配置并构建全部测试目标后注册 756 个 CTest，756/756 通过。首次主程序重链接被正在运行的 `Project_Raidline.exe` 占用并报 `LNK1168`；用户进程退出后重新构建，主程序链接成功。
- 最近 PR #60～#69 每项修改 27～82 个文件；production 11～30 个、测试 6～14 个、文档 6～12 个。
- `CURRENT_STATE.md`、`KNOWN_ISSUES.md`、`ROADMAP.md` 和 `src/app.cpp` 在 10/10 个切片中修改；CMake、GameSession、SaveRepository 和三个大型集成测试在 9/10 左右修改。
- production 热点：`app.cpp` 约 8k 行、`game_session.cpp` 约 2.5k 行、`gameplay_world.cpp` 约 1.8k 行；最大测试 `test_gameplay_world.cpp` 约 2.6k 行。
- CTest 原来 756 项全部无 LABEL；CMake 已保护唯一 cpp 所有者和 domain/simulation SDL-free，但没有自动检查向上 include、测试领域路由或 Projectile 权威回归。
- `active/` 中有 10 份已经合入的计划；项目状态文档保留当前分支、PR 和 CI 临时事实，且已出现 PR #68 合入后仍写“待 CI/验收”的漂移。
- Governor 默认要求读取 CURRENT_STATE、KNOWN_ISSUES、ROADMAP 和完整 ExecPlan；learning-analyst 仍以教学交接为职责，均不符合最小生产上下文。

## 3. 目标工作流

```text
Task request
-> fetch / preflight / generated snapshot
-> context route
-> short task envelope + risk gate
-> read-only exploration and impact search
-> one primary writer
-> focused area tests
-> Sentinel
-> Integration / migration / long sequence by risk
-> full regression
-> read-only reviewer + postflight
-> exact-head CI
-> user normal-play acceptance when visible
-> explicit merge authorization
```

上下文分为全局、领域、任务和历史四层。普通任务只加载 AGENTS、快照、一个主领域上下文、任务边界/ExecPlan 和直接相关符号；历史材料按证据需要加载。

## 4. 风险与证据门

| 风险 | 典型修改 | 必需证据 |
| --- | --- | --- |
| `local` | 文档、脚本、无状态局部表现 | envelope、相关检查、diff check；可见变化做人工验收 |
| `domain` | 单领域权威行为 | focused area、Sentinel、全量 CTest；C++ exact-head CI |
| `cross-domain` | GameSession/GameplayWorld/客户端串联或构建治理 | ExecPlan、各 area、Sentinel、Integration、全量、CI；可见变化人工验收 |
| `authority` | Profile、AssetRegistry、Save、schema、Settlement、Content 兼容、稳定 ID | 上述全部，加 migration、persistence、long-sequence 和幂等/失败原子性 |

范围软阈值只产生解释/拆分要求，不按行数硬切。任何超出 `allowed_paths`、进入 protected domain 或触碰 authority path 却未升级风险的修改都由 postflight 阻断。

## 5. 实施步骤与退出条件

1. 建立短 Context Index、六个领域上下文和机器路由图。退出：典型 Combat/Inventory/Persistence 任务只返回一个主上下文及按需邻域。
2. 建立 TOML Task Envelope 和治理脚本。退出：snapshot、route、preflight、impact、architecture、postflight 均可在 Windows/Python 标准库运行。
3. 给全部 C++ 测试目标分配 layer/area 标签，并建立约 20～50 个 Sentinel 注册。退出：`ctest -L` 可选择 Combat、Persistence、Sentinel、Integration 和 long-sequence。
4. 扩展架构保护和 CI。退出：向上 include、重复/遗漏 production cpp、SDL 越界、Projectile 权威回归会失败；GitHub 每个 PR 都跑治理门。
5. 收缩 AGENTS/Skills/Agents 和文档职责；归档已完成活动计划。退出：只保留一个业务 writer，不再强制教学分析或机械更新三份项目文档。
6. 用近期 Combat 和 Inventory 请求演练路由与影响面。退出：能定位正确上下文、标签和受保护领域，不默认读取 Persistence/Art/历史。

## 6. 验证、风险与回滚

- 自动化：治理 Python 单元测试、CMake configure、测试目标构建、Governance/Sentinel/area/integration/full CTest、`git diff --check`、architecture/impact/postflight。
- CI：exact-head Governance、Windows、Ubuntu。
- 人工窗口：不适用；本任务无玩家可见行为，开发代理不启动游戏。
- 风险：标签遗漏会造成假阴性，因此 CMake 对每个测试 target 缺标签直接失败；Sentinel filter 使用“无匹配即失败”；Context Map 与源码漂移由单测、架构守卫和实际 diff 路由演练发现。
- 回滚：文档/Skills 与自动化分成独立提交；整个分支可回退到 `origin/main@ba3375e`，不涉及数据迁移。

## 7. 进度

- [x] 完成 Git、PR、近 10 个切片、文档、Skills/Agents、CMake/CI、源码与测试审计。
- [x] 冻结最小上下文、风险、测试和单写者设计。
- [x] 完成仓库文件与工具实现。
- [x] 完成 Combat/Inventory 演练；两者均只选择一个主上下文，邻域按需展开。
- [x] 完成本地证据：Python 9/9、Governance 2/2、Sentinel 39/39、Integration 140/140、全量 CTest 796/796、ruff、mypy、architecture、documentation、impact 和 postflight 全部通过。
- [ ] 完成提交、推送、Draft PR 与 exact-head CI。

## 8. 实施证据

- CTest 从无标签的 756 项扩展为 796 项注册；新增注册来自同一测试二进制的 Sentinel 过滤器与 2 项治理测试，不重复编译生产源码。
- 796 项均具备 layer/area 标签；其中 `sentinel=39`、`layer-integration=140`、`long-sequence=3`、`governance=2`。
- 架构守卫确认：`raidline_domain=47`、`raidline_simulation=39`、`raidline_services=6`、`raidline_sdl_client=15`，不存在重复 production cpp、向上 include、SDL 越界或 Projectile 权威回归。
- 本分支生产代码、Content、Save schema、资产和 manifest 修改数均为 0；Draft PR #69 保持独立且未被修改。

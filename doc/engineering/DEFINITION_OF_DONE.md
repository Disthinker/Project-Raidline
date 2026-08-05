# Project Raidline Definition of Done

功能、Bug 修复或受控重构只有同时满足适用条目才可标记完成。环境阻塞时可以交付已完成部分，但状态必须是“部分完成/阻塞”，不能降低门禁或虚构 PASS。

## 代码

- 行为符合明确需求和玩家可感知结果。
- 范围与不做事项得到遵守，没有夹带未来系统或无关重构。
- 职责没有倒流；SDL 适配与可测试核心逻辑保持分离。
- 所有权、生命周期、类不变量和事务保证未被破坏。
- 没有修改、覆盖或重排无关文件；用户未提交修改完整保留。
- 新的 `.cpp`、资源或测试已接入每个需要它的 CMake/runtime 路径。

## 自动测试

- 新行为有成功路径测试。
- 无效输入、错误路径和边界有测试。
- 修改事务时，失败状态完全不变有快照或等价断言。
- 回归行为有测试，旧测试仍通过。
- 最小相关 target 构建成功，目标 CTest 通过。
- 全量 CTest 通过。
- 适用的非 CTest 检查（例如艺术 pytest）已执行或明确标记未执行。

## 工程与平台

- CMake configure 成功，完整 build 成功。
- 没有新增未处理 compiler/linker 警告或错误。
- Windows 与 Ubuntu 的 compiler、文件名大小写、依赖和 workflow 差异已考虑。
- 能读取 CI 时，检查精确 commit 的状态；不能读取时写“未验证”。
- 用 `ctest -N`、compile database 或构建日志证明新代码确实进入验证路径。

## 人工验收

- 给出准确启动和操作步骤以及预期结果。
- 能自动执行的由 Codex 执行并记录环境与观察。
- 需要视觉、手感或人工判断时提供验收清单。
- 未实际执行的项目保持 `未验证`，不能标 PASS。

## 审查

- 按 `$raidline-cpp-safety-review` 检查适用的 RAII、move-only、stable ID、vector 失效、const/noexcept、状态和事务风险。
- Reviewer 的可操作问题已修复并复验，或由用户明确接受为技术债。
- `git diff --check` 和最终 `git status` 已核对。

## 文档与教学

- `doc/project/CURRENT_STATE.md` 反映最终已验证状态。
- 架构或不变量发生变化时更新其唯一事实来源。
- 活动 ExecPlan 记录实际进度、决策、验证和偏差；全部完成后移入 `completed/`。
- DevLog 作为历史保留；需要时新增任务记录，不篡改旧状态。
- `doc/learning/CXX_LEARNING_LEDGER.md` 只追加本次真实学习证据。
- 在 `doc/handoffs/completed/` 生成中文 C++ 教学交接报告。

## 交付

- 汇报完成度、行为契约、改动文件、自动测试、人工验收、CI、技术债和测试债。
- 给出聚焦的 commit 划分、commit message 和 PR 标题/正文建议。
- 只有得到任务授权才 commit、push、创建 PR 或合并；禁止未经授权合并 `main`。

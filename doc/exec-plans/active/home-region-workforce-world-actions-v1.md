# Home Region 设施岗位现场操作 v1 ExecPlan

状态：开发完成，等待 exact-head Windows/Ubuntu CI 与用户正常游玩验收。分支 `codex/home-region-workforce-world-actions-v1` 从用户已验收并合入的 PR #139 / `origin/main@abe2886` 创建。

## 玩家结果与依赖

玩家在基地建设视图中可以直接点击工坊工作台或医疗床旁的聚合岗位占位，立即选中对应设施并看到岗位操作；右键岗位占位会打开只包含当前岗位相关命令的紧凑菜单。玩家无需先点中建筑本体或在多个设施面板之间寻找岗位入口，即可完成分配、清除和自动补员。

本切片依赖 PR #138 的稳定作业点与 PR #139 的聚合岗位世界投影，复用既有 `queryAssignBestBaseWorker`、`queryClearBaseWorker`、`queryAutoFillBaseWorkers` 及 GameSession 原子持久化命令。

## 领域与交互合同

- Workshop/Medical 的设施管理投影同时公开一个当前岗位命令（分配或清除）和一个自动补员命令；`canCommit` 与失败原因来自领域查询，App 不按人数或专业自行判断。
- Dormitory 继续保留单一自动补员入口，不产生重复按钮；其他设施不获得岗位命令。
- 作业点命中使用 PR #138 的稳定 `BaseFacilityWorkSocketProjection`，只对 PR #139 已发布岗位投影的 Workshop/Medical 生效；设施处于储备或不可见时没有幽灵点击区域。
- 左键岗位占位选中对应设施并打开现有检查器；右键岗位占位打开岗位专用菜单，只显示分配/清除和自动补员。建筑本体右键菜单保持现有功能与移动语义。
- 成功命令立即保存并刷新世界占位、检查器和菜单；拒绝或保存失败时 Profile、revision、岗位和世界时间均不改变。
- 本切片不增加存档字段、居民实例、排班、路径、动画或岗位资产所有权。

## 实施顺序与验证

1. 扩展设施管理投影，让两个真实岗位消费者复用领域查询发布自动补员操作，并验证操作集合无重复、查询零修改。
2. 增加作业点命中与岗位专用上下文菜单；左键选择和右键命令在任意建设缩放/平移下使用统一屏幕到世界投影。
3. 接入既有 GameSession 分配、清除和自动补员命令，补齐中英文状态与结果文本。
4. 重建受影响目标，运行设施管理、岗位、建设相机、双语、GameFlow 与持久化定向回归；完成 Windows Debug 全目标、完整 CTest 和 exact-head Windows/Ubuntu CI。
5. 交由用户正常游玩验收；Codex 不启动游戏。

人工验收：按 `B` 打开基地建设视图，在不同缩放和平移位置分别左键工坊/医疗岗位占位，确认检查器选中正确设施；右键岗位占位直接执行清除、分配和自动补员，观察岗位图形与状态立即刷新；无合格人员、设施不可用或已全部填满时按钮置灰且点击零修改；建筑本体右键的打开功能和移动操作保持正常。

## 风险、回滚与明确排除

主要风险是作业点和建筑命中重叠、菜单动作映射漂移。实现会优先岗位作业点命中，并按类型化 `BaseFacilityQuickActionKind` 过滤动作，而不是依赖行号或显示文本。改动仅涉及查询投影和客户端交互，普通 revert 即可回滚，不需要 schema/content 迁移。

本切片不实现具名 NPC、逐人居民实体、排班、寻路、工作动画、岗位拖放、工资、设施内部、正式美术/音频、实时守城或 AI 小队；不修改 Manifest。

## 完成证据

- Windows Debug 受影响目标编译通过：`Project_Raidline`、`BaseFacilityManagementTest`、`UiLocalizationTest`。
- 设施管理、双语、建设镜头、BaseWorld、GameFlow 与持久化定向回归通过：147/147。
- Windows Debug 全目标构建通过；完整 CTest 通过：1421/1421。
- 开发代理未启动游戏；正常游玩验收等待用户执行。

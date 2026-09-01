# Home Region 设施统一快速操作 v1 ExecPlan

状态：实现完成；Windows Debug 全目标、96/96 定向测试与 1385/1385 完整 CTest 已通过，等待 exact-head Windows/Ubuntu CI 与用户正常游玩验收。

## 玩家结果与依赖

依赖为用户已验收并以 merge commit `cb73ce0` 进入主线的 PR #128。开发分支 `codex/home-region-facility-quick-actions-v1` 从该精确 `origin/main` 创建。

玩家在基地建设模式选中设施后，可直接从紧凑管理卡片执行该设施当前最常用且无需额外选择的操作：为工坊或医疗所安排/清除人员，启动或取消对应升级，领取或取消工坊现有订单，启动居民治疗，以及从宿舍自动补齐空岗位。需要选择制造配方、休息时长、医疗对象详情或查看完整说明时，仍进入既有功能页，系统不会替玩家猜测选择。

## 查询、命令与客户端合同

- 扩展 SDL-free `BaseFacilityManagementProjection`，加入稳定 `BaseFacilityQuickActionKind` 与当前可执行性、阻塞原因、设施升级项目 ID；App 不根据按钮文字或颜色判断能力。
- 快速操作预览复用 `queryAssignBestBaseWorker`、`queryClearBaseWorker`、`queryAutoFillBaseWorkers`、`queryStart/CancelBaseConstruction`、`queryCollect/CancelBaseManufacturing` 和 `queryStartResidentTreatment`。
- 提交继续走 `GameSession` 已有原子事务和持久化边界。点击已过期或当前不可执行的操作只刷新阻塞消息，Profile、revision、货币、资产和任务状态不变。
- 工坊没有活动订单时不从卡片自动选择配方；卡片只提供人员、升级和详细页面入口。订单完成后显示领取，执行中显示取消。
- 医疗所只直接启动已由领域查询确定的下一项居民治疗；玩家付费医疗、授权物品管理等保留在完整医疗页面。
- 管理卡片保持紧凑双列鼠标按钮，禁用状态仍可点击查看具体原因；所有新按钮、回执和阻塞提示接入中英文切换。

## 验证与人工验收

自动化覆盖各设施动作集合、动作顺序、人员切换、升级开始/取消、制造领取/取消、居民治疗、自动补员、不可用设施与查询零修改；现有领域测试继续证明拒绝和保存失败零提交。完成 Windows Debug 全目标、定向测试、全量 CTest 与 exact-head Windows/Ubuntu CI 后交给用户正常游玩验收，Codex 不启动游戏。

本地证据：Windows Debug 全目标构建通过；设施管理、本地化、岗位、建设、制造、居民治疗、GameSession 与 GameFlow 定向测试 96/96 通过；完整 CTest 1385/1385 通过。

人工验收：在基地按 `B`，从工坊卡片安排/清除人员并启动/取消升级；生产完成后从卡片领取产物，生产进行中可取消并返还输入。医疗所卡片可安排人员、升级并在条件满足时启动居民治疗。宿舍可自动补齐空岗位。任一条件不足时按钮显示禁用，点击只提示原因且不改变状态；完整功能页仍可正常使用。

回滚为普通 revert。该切片不改变 schema、content version、资产位置或已有领域事务，回滚不会触发存档迁移。

## 明确排除

不自动选择制造配方、治疗物品或休息时长；不新增队列、多工位、居民个体面板、设施耐久/电力、固定设施移动/回收、正式美术或 Manifest 修改。

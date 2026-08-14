# Content Registry v1 ExecPlan

状态：实施中

产品范围来源：`E:\WorkPlace\Projects\C\Project RaidLine GDD\05_Core_Extraction_Alpha_首阶段功能规格.md`

上位计划：`doc/exec-plans/active/core-extraction-alpha.md`

代码基线：`origin/main@1837928`

实施分支：`codex/content-registry-v1`

## 1. 产品结果与完成定义

把当前散落在 C++ 中的 V0 物品、柜体 Loot、默认敌人部署和首图常量迁入一份版本化 JSON，由不可变 `ContentRegistry` 在启动构造阶段完整解析与验证。现有内容数量、位置、数值、贴图、手感和玩家流程保持不变；后续 Profile/AssetRegistry 与 Alpha 地图快照可以保存稳定命名定义 ID，而不依赖枚举值、显示名称或场景地址。

完成时必须同时满足：

- 存在类型隔离的 `ItemDefinitionId`、`LootTableDefinitionId`、`EnemyDeploymentDefinitionId` 和 `MapDefinitionId`，持久化兼容值为命名字符串。
- `ContentRegistry` 构造后不可修改，并拒绝不支持的 schema、重复 ID、非法引用、非法数值、越界/不连通首图配置和缺失发布资源引用。
- 当前五种物品、默认柜体 Loot、默认三敌人部署、首图世界/玩家/地面物/柜体/撤离/Raid 常量均由 `assets/content/v1/core.json` 驱动。
- 旧 `ItemId` 与当前运行时接口只作为一个迁移周期的显式适配器保留；显示名称不参与任何合法性判断。
- 本地 focused/full CTest 与 Windows/Ubuntu 精确 head CI 通过；因玩家可见行为不变，不新增真实窗口人工门禁，也不由开发代理启动游戏。

## 2. 基线、依赖与排除范围

- PR #55、#54、#56 已分别进入 main；当前接受基线为 Build Module Foundation 合并提交 `1837928`。
- PR #56 feature head `ef66dbd` 的范围检测、Windows 和 Ubuntu CI 全部成功；本分支从合并后的干净 `origin/main` 创建，不叠加未接受分支。
- 起始 Windows 基线注册 558 个 CTest。
- Week29 不进入本分支；正式攻击动画、新美术/音频、runtime PNG 与美术 manifest 修改继续暂停。

明确排除：

- 不建立 ProfileState、AssetRegistry、AssetLocation、存档 schema 或实例迁移。
- 不新增 Alpha 物品、4～6 人敌人组、三组出生撤离、Loot 插槽、无硬时限或经济内容。
- 不改变当前 `ItemInstance` 所有权、GridInventory 事务、随机算法、敌人 AI、伤害、地图几何或可见贴图。
- 不提供热更新、公开 Mod API、脚本层、通用资源系统或全局事件总线。
- 容器循环与价格套利验证随真正的容器/经济定义消费者进入后续切片；本 schema 不提前创建无消费者字段。

## 3. V0 内容差距

| 分类 | 起始状态 | 本 PR 处理 |
| --- | --- | --- |
| 可复用 | `ItemDefinition` 字段与五种已发布占位内容 | 保持值不变，增加稳定命名 ID 并由 JSON 构造 |
| 可复用 | `LootTable` 抽取与堆叠算法 | 算法不变，默认表由 Registry 定义适配 |
| 可复用 | `EnemySpawn`、`GroundItemSpawn` 和现有首图 | 运行时类型不变，默认配置改从 Registry 投影 |
| 需要重构 | `ItemId` 数组序号是唯一物品身份 | 改为强类型字符串 ID；枚举只保留过渡映射 |
| 需要重构 | GameplayWorld/App 内散落首图和资源路径常量 | 统一从已验证 `MapDefinition` 读取 |
| 需要新建 | 无内容 schema、版本和引用校验 | 建立 JSON schema v1、不可变 Registry 与验证测试 |
| 停止扩展 | 在 C++ switch/显示名中继续增加正式内容 | 后续内容先进入 JSON 定义与校验，再由领域消费者引用 |

## 4. 所有权、接口与迁移影响

- `ContentRegistry` 唯一拥有不可变定义值和按强类型 ID 建立的索引；查询只返回 `const` 引用。
- `DefinitionId<Tag>` 自身拥有稳定字符串并验证基础命名语法；不同定义域不能隐式互换。
- `ItemDefinition` 同时保存强类型 `definitionId` 与过渡 `ItemId id`。`itemDefinition(ItemId)`、`itemDefinitions()` 和运行时 `ItemInstance` 暂由适配器映射到 Registry；下一项 Profile Asset Registry 迁移实例身份后删除旧枚举依赖。
- 默认 `LootTable`、敌人部署和地图运行时对象由 Registry 投影生成，不保存对 JSON 节点、临时字符串或 vector 下标的引用。
- JSON 作为发行内容输入在 CMake 配置时嵌入只读生产代码；整个 `assets/` 仍按原路径复制。发布资源引用由 Registry 逻辑校验，并由测试核对物理文件存在。
- 本 PR 不产生存档格式，因此没有迁移或回滚数据；稳定命名 ID 为后续 schema v1 提供输入。

## 5. 实施步骤与退出条件

1. **冻结基线**：fetch、确认 PR #56 合入/CI、`origin/main@1837928` 与干净工作区。退出条件：独立分支已从精确主线创建。
2. **建立 ID 与 Registry**：实现强类型 ID、定义 DTO、JSON 解析和完整当前 schema 校验。退出条件：合法最小/发行内容可加载，重复 ID、坏引用和非法字段均拒绝。
3. **迁移当前内容**：加入 `assets/content/v1/core.json`，物品目录、默认 Loot、敌人部署、首图/背景/柜体/撤离/Raid 常量全部从 Registry 投影。退出条件：旧测试合同和当前数值不变。
4. **保留迁移适配器**：枚举映射覆盖五个现有定义，非法/缺失映射失败。退出条件：所有现有消费者继续编译，强类型查询有直接测试消费者。
5. **回归与交付**：更新架构/状态/路线/构建文档，完成 focused、全量 CTest、提交、推送、PR 和 exact-head CI。退出条件：范围、Windows、Ubuntu 全绿且 PR 可审查。

## 6. 验证与证据

自动化门槛：

- ID：合法命名字符串往返；空值、无命名空间、路径/空白/非法字符拒绝；不同标签编译期不可互换。
- Registry：schema/content version、重复资源/定义/旧枚举映射、缺字段、非法类别/尺寸/堆叠/路径/引用/权重/数量、无敌人部署、越界首图配置均失败。
- 当前内容：五项物品、100 权重三次柜体抽取、三名默认敌人、六项地面物、1280×720 首图、玩家/柜体/撤离/180 秒/3 秒值与迁移前一致。
- 资源：所有 `published_resources` 物理文件存在；所有可见物品和背景引用都属于该集合。
- 回归：ItemDefinition/LootTable/GameplayWorld/GameSession/App 相关 focused tests、全量 CTest、Windows/Ubuntu CI。

人工验收：本 PR 不改变玩家可见行为，不安排新真实窗口清单；开发代理不启动游戏。若自动化或 CI 表明资源定位/运行时构造发生变化，再把最小启动与现有 V0 清单放在全部自动化之后交给用户验证。

证据格式：分支、commit、基线、内容 schema/version、Preset、focused/full 数量、资源校验、CI URL/状态、人工验收是否适用、偏差与未验证风险。

## 7. 提交、PR、风险与回滚

- 一个 focused 分支和一个 PR；领域/内容实现与同步文档可分 coherent commits。
- 主要风险是静态初始化失败、旧枚举映射遗漏、资源路径漂移、JSON 数值类型差异和 GameplayWorld 默认值回归；通过构造期全量验证、物理资源测试、旧合同测试与全量 CI 收口。
- Registry 只在启动构造期解析一次，不在帧循环读取 JSON；不暴露可变引用或热更新入口。
- 回滚为完整 revert 本 PR，恢复 C++ 静态目录和常量；没有存档或资产实例数据需要迁移。

## 8. 进度记录

- [x] 完整读取范围合同、治理/架构/测试规则和总 ExecPlan。
- [x] fetch 并确认 PR #56 已合入、精确 head CI 成功、`origin/main@1837928` 与工作区干净。
- [x] 从精确主线创建 `codex/content-registry-v1`。
- [ ] 建立强类型 DefinitionId、不可变 ContentRegistry 与 JSON v1 校验器。
- [ ] 迁移物品、Loot、敌人部署和首图常量并保留显式枚举适配器。
- [ ] focused/full CTest 与发布资源存在性检查通过。
- [ ] 提交、推送、创建 PR，并完成精确 head Windows/Ubuntu CI。

最后更新：2026-08-14。

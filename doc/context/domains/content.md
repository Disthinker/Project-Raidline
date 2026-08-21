# 领域上下文：内容定义

## 权威与入口

- `assets/content/v1/core.json` 是当前发布内容输入；`ContentRegistry` 构造后不可变。
- 主要入口：`src/content_registry.h`、`src/item_definition.h`、`src/definition_id.h`、`src/alpha_content_ids.h`。
- 定义 ID 是命名字符串强类型值；运行时索引、枚举序号、显示名称都不能进入正式存档身份。

## 稳定执行路径

`JSON -> schema/字段/引用/资源验证 -> 不可变定义 -> domain/simulation const 查询 -> Profile 只保存 DefinitionId`。

## 核心不变量

- 重复 ID、非法引用、非法范围、价格套利、越界地图锚点和缺失发布资源必须阻断加载。
- Content version 与 Save schema 分离；内容兼容变化不得静默拒绝已有合法档案。
- 已开始 Raid 使用冻结快照，不被内容重载或后续随机抽取改变。
- Runtime 只读取批准的 `assets/`；`art/work/` 不是发布源。

## 主要消费者与测试

- 消费者：Profile 初始资产、Inventory/Weapon/Medical/Economy、地图与部署、SaveRepository。
- CTest：`ctest -L area-content`、`ctest -L sentinel`。
- 关键测试：`test_content_registry.cpp`、`test_item_definition.cpp`、相关 SaveRepository 兼容测试。

## 跨域风险与禁止事项

新增实例字段或兼容规则时加载 `session-persistence`；新增资产能力时加载 `asset-inventory`；武器/敌人规则加载 `combat`。不得把公开 Mod、热更新或未授权资源生产顺带加入普通内容切片。

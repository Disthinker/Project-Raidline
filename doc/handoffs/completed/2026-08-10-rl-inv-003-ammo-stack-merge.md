# RL-INV-003 弹药整堆拖放合并 C++ 教学交接

## 1. 任务名称与状态

- 任务：修复普通整堆 9mm 弹药拖到同定义已有堆时不能合并。
- 日期/分支/基线：2026-08-10 开始、2026-08-11 验收，`codex/rl-inv-003-ammo-stack-merge`，基线 `origin/main@61718f6`；本文件随功能提交交付，精确 SHA 以 Git 历史为准。
- 完成度：代码、文档、Windows Debug 全目标构建、本地自动测试和真实窗口验收完成；精确提交 CI 待完成，问题状态为 `Local Fixed`。

## 2. 用户可见结果

- 普通整堆拖拽到同定义未满弹药堆时，预览可显示合法并在释放后合并。
- 目标堆最多补到 60；目标容量不足时，源堆余量以原 ID 留在原位置。
- 同容器和玩家/柜体跨容器使用相同规则；满堆、定义不匹配或不可堆叠目标仍拒绝且不改变库存。
- Ctrl/Shift 数量拖拽仍要求精确数量完整提交，不包含 RL-INV-001 交换或 RL-INV-002 点击锁定。

## 3. 修改文件与核心符号

| 文件 | 核心符号 | 作用 |
| --- | --- | --- |
| `src/inventory_transfer.h/.cpp` | `canPlaceWholeItemAt`、`tryPlaceWholeItemAt` | 普通整堆放置的统一查询与事务命令 |
| `src/app.cpp` | `handleInventoryPointerEvent`、`renderInventoryPlacementPreview` | 普通拖拽预览和释放接入同一领域规则 |
| `tests/test_inventory_transfer.cpp` | `InventoryWholeStackPlacementTest` | 同/跨容器、完全吸收、溢出、失败不变和查询无副作用 |
| `tests/test_mouse_inventory_interaction.cpp` | `MouseInventoryIntegrationTest` | 用真实普通鼠标请求验证整堆领域入口 |

## 4. 修改前后的执行路径

- 修改前：普通拖拽请求不带 `selectedQuantity`，App 直接调用 `tryTransform` 或 `tryTransferItemTransform`；已占用目标格必然非法。
- 修改后：普通拖拽预览调用 `canPlaceWholeItemAt`，释放调用 `tryPlaceWholeItemAt`。空目标委托原 transform/transfer；匹配未满目标按容量合并。
- 数量拖拽仍走 `canPlaceItemQuantityAt` / `tryPlaceItemQuantityAt`，没有改变精确数量合同。

## 5. 关键设计决策

- 新增整堆放置入口，不放宽已有精确数量 API，避免 Ctrl/Shift 选择 10 个却只提交 3 个。
- 溢出策略冻结为“目标补满、源余量原地保留”，不创建第三个堆叠。
- 合并保留目标 ID；源完全吸收时删除源，部分吸收时保留源 ID、origin、orientation。
- App 只负责编排引用和请求，合法性与 mutation 规则留在 `inventory_transfer`。

## 6. C++ 语言与标准库

- 语言特性：`const` 查询、`[[nodiscard]]`、值类型 ID 与显式引用参数。
- 标准库组件：`std::optional`、`std::min`、`std::uint32_t`。
- 指针只在 mutation 前短期读取；跨 `remove` 不保存 `PlacedItem*`，只保存稳定 ID 和数量值。
- 查询不标 `noexcept`，与既有 inventory 查询边界保持一致；命令对预验证后不应失败的内部不变量使用既有 `std::terminate` 策略。

## 7. 所有权与生命周期

- `GridInventory::PlacedItem` 继续唯一拥有 `ItemInstance`。
- 完全合并时源 inventory 移除并销毁被吸收的源实例；目标实例不移动、不换 ID。
- 同容器 `remove` 会使 vector 指针/引用失效，因此删除后按已复制的目标稳定 ID 调用受控数量命令。
- 部分合并不移动所有权，只修改源/目标数量；两个 placement 的位置、方向和 ID 均保持。

## 8. 数据结构、算法与复杂度

- 查询通过 placement 线性查找源和目标，使用目标格的 occupant ID 解析实例。
- 合并量为 `min(sourceQuantity, maxStackSize - destinationQuantity)`。
- 当前查找时间复杂度为 `O(n)`，提交额外空间 `O(1)`；与既有扁平 placement 模型一致。

## 9. 状态机与事务规则

- 普通拖拽状态机不变，只更换 release 后的领域命令。
- 查询成功不修改 inventory；提交前再次执行同一规则。
- 空格成功后保持原移动/旋转/跨容器所有权转移语义。
- 合并成功后目标不超过上限；失败时两侧 placement、cells、ID、数量和方向完全不变。
- Esc/Tab 和数量拖拽仲裁未改动。

## 10. 真实问题与修复

| 类别 | 现象 | 根因 | 最终修复 | 验证 |
| --- | --- | --- | --- | --- |
| 编译 | 首次构建命令在 Dev Shell 后从默认目录查找 preset | `Launch-VsDevShell.ps1` 自动切换工作目录 | 加载后显式 `Set-Location` 到仓库 | 后续 configure/build 成功 |
| 测试 | 非法目标测试中的 Medkit 无法放入 1×1 测试网格 | 测试夹具 footprint 不合法，尚未进入目标行为 | 改用 1×1 Cola 作为定义不匹配目标 | 聚焦与全量测试通过 |
| 链接 | 未发生 | 未发生 | 无需修复 | 全目标链接成功 |
| 运行 | 未发生运行库或 `gtest_ar_` 错误 | 未发生 | 无需修复 | 552/552 通过 |

## 11. 验证证据

- Configure：Visual Studio Developer Shell，x64 host/x64 target，UTF-8，`windows-debug` preset 成功。
- Build：`cmake --build --preset windows-debug --parallel`，226 步全目标重建成功，包含主程序。
- 目标测试：实现前两个测试目标因新 API 不存在而预期编译失败；实现后聚焦 45/45 通过。
- 全量 CTest：552/552 通过，0 失败，4.83 秒。
- `git diff --check`：通过。
- CI：未执行，分支尚未提交/推送。
- 人工验收：用户于 2026-08-11 确认活动 ExecPlan 的同容器、跨容器、溢出余量、非法目标和精确数量回归 1–5 全部通过。

## 12. 教学分级

- 已接触、可快速复习：稳定 ID、move-only ItemInstance、查询/命令分离、失败不变。
- 可能仍不稳定、应重点讲：同容器 erase 的引用失效、预验证后的无失败提交、两种数量合同的区别。
- 本次首次出现：普通整堆的“部分填满但成功”合同与精确数量原子合同并存。
- 重复样板、无需展开：GTest 基础断言、CMake target 列表和 App SDL 绘色代码。

## 13. 复盘问题

1. 为什么不能直接把 `tryPlaceItemQuantityAt` 改成“能合并多少算多少”？
2. 同一 inventory 删除源 placement 后，为什么不能继续使用删除前取得的目标指针？
3. 为什么目标容量不足时源堆应留在原位置，而不是移动到鼠标目标附近？
4. 预览和提交为什么必须共享同一领域规则？
5. 合并为什么保留目标 ID，而不是保留源 ID 或分配新 ID？

## 14. 文件与函数定位

- `src/inventory_transfer.cpp`：`canPlaceWholeItemAt`、`tryPlaceWholeItemAt`。
- `src/app.cpp`：`App::handleInventoryPointerEvent`、`App::renderInventoryPlacementPreview`。
- `tests/test_inventory_transfer.cpp`：`InventoryWholeStackPlacementTest`。
- `tests/test_mouse_inventory_interaction.cpp`：`MouseInventoryIntegrationTest.WholeStackDragMergesAtOccupiedCell`。

## 15. 技术债与测试债

- 技术债：App 仍直接编排两个 `GridInventory&`，但本任务没有扩大其规则职责。
- 测试债：缺少 App 级自动 UI/截图测试；本次绿色/红色预览和真实 release 手感已由用户人工验收，后续同类回归仍需保留人工检查。
- 下一安全任务：冻结本分支代码并完成精确提交 CI；之后再独立选择 RL-INV-001 或 RL-INV-002，不夹带到本分支。

## 16. 可复制给网页端 GPT 的教学 Prompt

```text
你是我的 C++ 学习教练。不要修改 Project Raidline 的项目代码。

请只根据 doc/handoffs/completed/2026-08-10-rl-inv-003-ammo-stack-merge.md 和对应真实 diff 教学。重点解释 canPlaceWholeItemAt/tryPlaceWholeItemAt 如何区分空格移动与占用格合并、为什么普通整堆允许部分填满而 Ctrl/Shift 必须精确提交、同容器 remove 后的指针失效，以及稳定 ID/源余量/目标上限不变量。请明确区分本地自动测试、尚未执行的真实窗口验收和 CI。
```

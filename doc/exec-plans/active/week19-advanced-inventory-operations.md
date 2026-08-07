# Week19 高级背包操作 ExecPlan

状态：In Progress
负责人/工作流：主线程；`raidline-feature-delivery` + `raidline-inventory-domain`
最后更新：2026-08-07

## 目标与玩家结果

在 Week18 已合入的玩家背包与柜体双容器基础上，交付一组适合后续 Loot 扩展的高频背包操作：

1. 双容器界面中，鼠标悬停物品后按 `F` 或 `Ctrl + 右键`，把整件物品按 row-major first-fit 快速转移到另一侧。
2. 拖拽物品期间按 `R`，围绕当前抓取点顺时针旋转 90°，预览与最终 footprint 使用旋转后的尺寸。
3. 弹药类 1×1 物品支持数量堆叠；`Ctrl + 左键` 拿起 1 个，`Shift + 左键` 拿起一半，数量虚影随鼠标移动并在 release 时精确放置、合并或丢弃。

第 1、2 项已经完成自动化与真实窗口验收。第 3 项第一版的领域模型、输入接线、数量角标、355/355 本地自动化与真实窗口 1–8 项均已通过；随后用户修订产品语义：Ctrl/Shift+左键不应立即 first-fit 转移，而应在 PlayerOnly 和双容器界面按数量拿起并进入拖拽。修订版实现、367/367 自动化与真实窗口 1–8 项现已全部通过，当前只待冻结提交与最终 PR CI。

## 当前仓库基线

- 基线：`main` / `origin/main` 的 `7e436aa`，PR #33 已合入 Week18 双容器转移、柜体交互、右侧丢弃区、脚下掉落和纯鼠标背包。
- 本地分支：`codex/week19-advanced-inventory-operations`，从 `7e436aa` 创建，起始工作区干净。
- `GridInventory` 使用 move-only `ItemInstance`、稳定 ID、row-major cells 和完整 footprint。
- `inventory_transfer` 已提供指定格与 first-fit 的跨容器事务；失败时两个容器不变。
- `InventoryInteractionState` 在 Idle 时仍保存 `hoveredLocation`，但不绘制持久高亮；这可作为快捷操作的目标，不需要恢复蓝色框。
- `App::processEvents` 当前只暂存 motion/左键事件；`F` 在背包关闭时是世界交互，背包打开时被世界输入屏蔽。
- 自动化基线为 Windows Debug 全量 CTest 304/304；PR #33 的 Windows、Ubuntu 和范围检查已通过，动态证据记录在 PR #33。
- `doc/project/CURRENT_STATE.md`、`ROADMAP.md` 已在最终收口同步；Week18 按 PR #33 的合入事实归档，不单独制造 CI 轮次。

## 行为契约

### 已完成：整件快捷转移

- 背包关闭时，`F` 保持现有世界交互语义，包括靠近柜体打开双容器界面。
- 只有 `InventoryOverlayMode::Container` 允许快捷转移；PlayerOnly 模式没有目标容器，快捷键无副作用。
- 鼠标必须悬停在实际占用格上；通过 `occupantAt` 解析稳定 ID，多格物品的任意覆盖格都指向同一实例。
- `F` 与 `Ctrl + 右键` 均把悬停物品从当前面板转移到另一面板，调用 `tryTransferItemFirstFit`。
- 快捷转移只在 `InventoryPointerPhase::Idle` 生效；按压或拖拽期间不会产生第二次提交。
- 右键快捷操作在 button-down 触发；对应 button-up 不产生请求。
- Ctrl 状态必须随 SDL 事件顺序确定，不能在帧末读取一个可能已经变化的全局修饰键状态。
- Tab 关闭与 Esc 取消继续优先于本帧全部快捷转移和鼠标释放；被取消帧不得修改任一容器。
- 成功后清理可能失效的临时选择；失败时 hover 可保留，两个容器和稳定 ID 完全不变。
- 不绘制新的悬停或选中边框。

### 当前阶段：拖拽旋转

- `R` 仅在 Dragging 阶段生效；每次非重复 key-down 顺时针旋转 90°，Esc/Tab 取消不提交。
- 快速转移保持已有朝向，不自动旋转找空位。
- 方向是 `ItemInstance` 的运行时状态；候选方向只存在于拖拽状态，合法 release 才与位置一起提交。
- 抓取格锚点按 `(x, y) -> (oldHeight - 1 - y, x)` 旋转；连续像素锚点按 `(x, y) -> (oldHeight - y, x)` 旋转，确保虚像不从指针下跳走。
- Pistol/Rifle 支持 0/90/180/270 四向旋转；Cola/Medkit 不可旋转。
- 同容器、跨容器与玩家脚下丢弃都使用候选方向；非法释放、Esc、Tab 保持原 origin、orientation 和 cells。
- 地面物品拾取范围与世界纹理使用旋转后尺寸；快捷转移保留当前方向并按该 footprint first-fit。

### 后续阶段默认契约

- `Ctrl + 左键` 立即拿起 1；`Shift + 左键` 立即拿起 `(quantity + 1) / 2`；源栈在 release 前不修改，Ctrl+Shift 同时按下不执行。
- 数量拿取在 PlayerOnly 与双容器界面都可用；release 到精确空格时拆分新栈，到同类未满栈时精确合并，到玩家丢弃区时只丢出所选数量。
- 普通物品最大堆叠为 1；首个弹药定义建议 1×1、不可旋转、最大堆叠 60。
- 部分转移必须完整满足请求数量，否则两个容器完全不变。

## 范围与不做事项

### 本计划范围

- 背包 UI 输入意图、事件顺序与修饰键快照。
- 双容器 first-fit 快捷转移。
- 物品四向旋转、有效 footprint 和拖拽预览。
- 数量堆叠、拆分/合并、部分跨容器转移与地面拾取整合。
- 最小弹药定义、批准资源接入、数量角标、自动测试、人工验收、文档和教学交接。

### 明确不做

- 随机 Loot、搜索计时、RaidSession、Stash、重量、耐久、装备栏、上下文菜单。
- 自动旋转找空位、跨多个柜体、排序、持久化。
- 大规模拆分 `App`、ECS、SceneManager 或通用命令框架。
- 为旋转后的物品制作第二套贴图；运行时由 SDL 旋转既有批准纹理。

## 主要类型、调用路径与所有权

当前快捷转移路径：

`SDL event -> App 输入适配 -> InventoryInteractionState::hoveredLocation -> GridInventory::occupantAt -> tryTransferItemFirstFit`

- `InventoryInteractionState` 只保存容器 ID、格子、稳定实例 ID 和指针状态，不拥有 `ItemInstance`。
- `GridInventory::PlacedItem` 独占 `ItemInstance`；快捷转移沿用 `inventory_transfer` 的 remove/place 事务。
- 当前阶段优先增加小型、纯函数式快捷操作仲裁，不把 SDL 类型传入 inventory core。
- 后续拆栈需要新稳定 ID；ID 分配仍由 `GameplayWorld` 的唯一序列负责，不允许 UI 自行生成 ID。

## 不变量

继续遵守 `doc/architecture/INVARIANTS.md`，并新增：

- 快捷操作根据稳定 ID 提交，不跨 mutation 保存 vector 引用、迭代器或下标。
- 快捷转移查询无副作用；失败保持源/目标 placement、cells、顺序、实例和 ID 不变。
- 同一帧 Tab/Esc 必须在快捷转移、旋转、部分转移或 release 之前生效。
- Idle hover 是瞬态命中信息，不等于持久选中，也不要求视觉高亮。
- 后续一个稳定 ID 标识一个物品栈 placement；拆分创建新 ID，合并保留目标 ID。

## 实施里程碑

### M1：整件快捷转移（已完成）

1. 先为纯输入仲裁补充失败测试：Container/PlayerOnly、Idle/Dragging、空 hover、`F`、Ctrl+右键、Tab/Esc 同帧。
2. 扩展设备无关背包输入事件，使右键 button-down 带有 Ctrl 快照；`F` 在背包打开时进入背包队列，关闭时继续作为世界交互。
3. 从 hover 解析源容器与稳定 ID，目标固定为另一侧容器，调用 `tryTransferItemFirstFit`。
4. 添加 App 边界的最小路由测试或纯仲裁测试，不重构整个 App。

退出条件：聚焦测试与全量 CTest 通过；真实窗口中 F 和 Ctrl+右键可双向移动，PlayerOnly、空格、满容器、拖拽中和取消帧均不写入。

### M2：拖拽旋转（已完成）

1. 为 `ItemDefinition` 增加 `canRotate`，为 `ItemInstance` 增加四向 orientation，并提供有效 footprint 查询。
2. `GridInventory` 增加无副作用 transform 查询和原子 move+rotate 提交；旧 `canMove`/`tryMove` 委托给当前方向以保持兼容。
3. 交互状态保存候选方向与旋转后的 grab anchor；ghost 围绕当前鼠标抓取点旋转。
4. release 请求携带候选方向；同容器 transform、跨容器 transfer 和 drop 成功时提交，取消时不提交。

退出条件：边界、碰撞、自重叠、四次旋转、非法释放和 Esc/Tab 回滚均由测试证明，真实窗口虚像无明显跳跃。

### M3：堆叠与部分转移（逻辑、资源与自动化完成，待最终复验和人工验收）

1. `ItemDefinition` 增加 `maxStackSize`，`ItemInstance` 增加有效 quantity；普通物品保持 1。
2. 建立纯查询 transfer plan：先按稳定 placement 顺序填充相同定义的未满栈，再为余量找 row-major first-fit。
3. 预留所有可能分配后再提交数量变化、移除和放置；拆分所需 ID 只在确定成功的提交路径消耗。
4. Ctrl+左键和 Shift+左键生成带所选数量的立即拖拽；release 请求携带数量，支持同背包/跨容器精确放置与合并、部分脚下丢弃、数量角标和原子拾取合并。
5. 通过独立 art pipeline 包接入首个批准弹药资源；资源未批准前不使用临时图片进入 `assets/`。

退出条件：成功、满栈、奇偶数量、无空间、ID 语义、合并顺序、失败快照和地面往返全部通过。

### M4：收口

1. 按 `doc/engineering/BUILD_AND_TEST.md` 运行聚焦目标、全目标构建和全量 CTest。
2. 完成 C++ 所有权/失效/事务审查和真实窗口人工验收。
3. 同步 CURRENT_STATE、ROADMAP、PROJECT_OVERVIEW、ARCHITECTURE、INVARIANTS 和问题台账；归档 Week18/Week19 计划状态。
4. 创建中文 C++ 教学交接；冻结代码与文档后提交一个 Week19 PR，并只等待一次 CI。

## 自动测试矩阵

| 层级 | 必测行为 |
| --- | --- |
| 输入/仲裁 | F、Ctrl+右键、修饰键按下/释放顺序、按键重复、Tab/Esc 优先、拖拽中拒绝 |
| 交互状态 | hover 命中/空格/面板外、多格任意覆盖格、PlayerOnly 无目标 |
| 跨容器事务 | 双向 first-fit、满目标、缺失 ID、相同容器、失败快照、稳定 ID 保持 |
| 旋转 | 0/90/180/270 footprint、抓取锚点、边界、碰撞、自重叠、取消回滚 |
| 堆叠 | 数量边界、整栈/1个/一半、奇偶、合并、新栈、ID 分配、容量失败 |
| 世界整合 | 整栈脚下丢弃、拾取合并、无容量不变、世界 F 交互回归 |

M1 聚焦目标至少包括 `InventoryInteractionTest`、`InventoryTransferTest`、`InputSystemTest` 和相关鼠标交互测试；M2 增加物品定义/实例、GridInventory transform、跨容器 transform、世界丢弃与鼠标事务覆盖；M3 增加 quantity 边界、稳定顺序合并、拆分 ID、容量回滚、修饰键、指定格放置与地面往返。当前 Windows Debug 全量为 367/367。新增源必须由专用 CMake target 编译，并在 `compile_commands.json` 中可见。

## 人工验收草案

M1：

1. 靠近柜体按 F 仍只负责打开双容器界面。
2. 悬停玩家物品按 F，整件进入柜体 first-fit。
3. 悬停柜体物品按 F，整件返回玩家背包。
4. Ctrl+右键完成同样的双向转移。
5. PlayerOnly、空格、目标满载、拖拽过程中快捷键均不修改物品。
6. Tab/Esc 与快捷操作同帧时优先关闭/取消，不发生转移。
7. 快捷转移后没有持久蓝色高亮，原拖放和脚下丢弃不回归。

M2：

1. 拖动 Pistol 或 Rifle，按一次 R 后虚像与候选 footprint 顺时针旋转 90°，抓取点仍留在鼠标下。
2. 连续按四次 R 恢复原方向；Cola 和 Medkit 拖动时按 R 不旋转。
3. 在玩家背包内合法释放后，新方向、origin 与覆盖格一起提交。
4. 玩家背包与柜体之间拖动并旋转，双向合法释放均保留新方向。
5. 靠近边界或与其他物品碰撞的非法释放不改变原位置、方向或两个容器。
6. 旋转后按 Esc 取消、按 Tab 关闭，均不提交；Tab/Esc 与 release 同帧仍以取消为先。
7. 旋转后的玩家物品拖到右侧丢弃区，物品出现在角色脚下并保持方向；重新拾取后方向仍保留。
8. M1 快捷转移、普通拖放、无 Idle 蓝色高亮和方向键无库存语义均不回归。

M3 修订版：

1. 仅打开玩家背包时，对弹药按 Ctrl+左键立即出现数量 1 的平滑虚影，源栈数量在鼠标松开前不变。
2. Shift+左键对偶数拿起一半，对奇数向上取整；虚影角标显示所选数量；Ctrl+Shift+左键不移动物品也不开始拖拽。
3. 所选数量可在同一背包释放到精确空格形成新栈，或释放到同类未满栈精确合并；目标满载、异类占用、越界或空白区释放均不修改源栈和 ID。
4. 打开柜体后，所选数量可从玩家背包拖到柜体，也可从柜体拖回玩家背包；落点使用鼠标指定格，不使用 first-fit 快速转移。
5. 玩家所选数量拖到右侧丢弃区后，只把该数量作为新地面栈放到角色脚下；源栈保留原 ID 和剩余数量。柜体物品仍不能直接丢弃。
6. 拆分到空格或地面才创建新稳定 ID，合并保留目标 ID；失败与取消不消耗 ID。
7. F、Ctrl+右键继续执行整栈 first-fit 快速转移；普通拖放、R 旋转、数量角标、拾取合并与方向键无库存语义均不回归。
8. Tab/Esc 与数量 release 同帧时仍优先关闭/取消，源/目标/地面均不修改。

## 风险与失败语义

- **F 双重语义**：只按 overlay mode 分流；Closed 属于世界，Container 属于快捷转移，PlayerOnly 无操作。
- **同帧重复提交**：Tab/Esc 先于 UI 队列；pointer 非 Idle 时快捷转移拒绝；每个物理按下只生成一次命令。
- **事件修饰键漂移**：Ctrl 状态按事件顺序快照，不在 update 末尾读取最终键盘状态。
- **引用失效**：hover 只解析稳定 ID；mutation 后重新查询，不保留 `PlacedItem&`。
- **后续拆栈 ID**：不从 UI 拼接 ID；在全事务预检和预分配完成后调用 GameplayWorld 唯一分配器。
- **App 继续膨胀**：只提取可测试的小型输入仲裁/命令解析，不做大范围生命周期重构。
- **弹药资源缺失**：堆叠模型可先测试，但运行时弹药内容必须等待批准资源，不用占位图绕过流程。

## 决策日志

- 2026-08-07：用户将高级背包操作提升为下一开发任务；原路线图 Week19 最小 Loot 顺延。
- 2026-08-07：选择一个长期分支、分阶段本地验证和一个最终 PR/CI，减少远端等待次数。
- 2026-08-07：M1 只实现整件快捷转移；旋转和堆叠保留为后续明确里程碑。
- 2026-08-07：F/快捷鼠标操作以 hover 为目标，但不恢复持久选中高亮。
- 2026-08-07：Shift 一半采用向上取整；Ctrl+Shift 同时按下为无操作。

## 进度记录

- 2026-08-07：从 `main@7e436aa` 创建 `codex/week19-advanced-inventory-operations`；确认起始工作区干净。
- 2026-08-07：完成仓库基线、现有事件路径、first-fit 事务和稳定 ID 风险调查；M1 进入测试先行阶段。
- 2026-08-07：先增加快捷转移决策、Ctrl 按键状态和按格转移测试；首次聚焦构建按预期因缺少 `isControlPressed`、`decideInventoryQuickTransfer` 与 `tryTransferItemAtCellFirstFit` 失败，证明新测试先于实现生效。
- 2026-08-07：完成 M1。App 将鼠标与快捷转移统一规范化到帧级 UI 队列；`F` 与 `Ctrl+右键` 在 Container/Idle/hover 命中时按稳定 ID 双向 first-fit 转移，PlayerOnly、Pressed、Dragging、空格和失败事务不写入。
- 2026-08-07：M1 聚焦回归 47/47 通过；Windows Debug 全目标构建和全量 CTest 314/314 通过；`InputSystemTest` 20/20、`InventoryInteractionTest` 20/20、`InventoryTransferTest` 12/12 直接运行通过，未出现运行库或 `gtest_ar_` 栈损坏。
- 2026-08-07：通过 `ctest -N` 与 `compile_commands.json` 核验 314 个测试已注册，三个新增/修改测试源确实进入对应目标；`git diff --check` 通过。M1 尚待真实窗口 1–7 项人工验收，CI 按约定留到 Week19 单一最终 PR。
- 2026-08-07：用户确认 M1 真实窗口 1–7 项全部通过；整件 F / Ctrl+右键双向快捷转移达到退出条件，进入 M2 拖拽旋转。
- 2026-08-07：M2 开始前确定提交边界：按 R 只修改交互层候选方向与抓取锚点；同容器移动、跨容器转移或玩家丢弃仅在合法 release 时一次性提交方向，Esc/Tab/非法 release 保持原 placement 与方向。
- 2026-08-07：M2 采用测试先行；物品四向 footprint、实例方向、同容器 transform、跨容器 transform、抓取锚点、释放方向和脚下丢弃方向测试最初因接口尚不存在而按预期失败，随后完成实现。
- 2026-08-07：新增两条鼠标层实际事务测试，覆盖旋转后同容器 `tryTransform` 与跨容器 `tryTransferItemTransform`；`MouseInventoryInteractionTest.exe` 干净直跑 19/19，无运行库弹窗。
- 2026-08-07：最终在固定 UTF-8 工具输出下 fresh configure、117 步干净全量构建成功，Windows Debug CTest 335/335 通过；`git diff --check` 通过。M2 仅待真实窗口 1–8 项验收。
- 2026-08-07：用户确认 M2 真实窗口 1–8 项全部通过；旋转里程碑关闭并进入 M3。
- 2026-08-07：M3 测试先行，新增定义/quantity/拆分合并/容量失败/地面拾取测试最初因 `Ammo9mm`、quantity 与精确数量事务接口不存在而按预期编译失败，随后完成实现。
- 2026-08-07：`GameplayWorld` 保持拆分 ID 唯一分配权；精确数量事务按稳定 placement 顺序合并，再 row-major 放置，只有成功创建新栈才推进 ID。地面拾取与整栈丢弃保留 quantity 和 ID。
- 2026-08-07：Ctrl/Shift 左键事件、向上取半、Ctrl+Shift 无操作与数量角标已接入；Windows Debug 全目标构建及 CTest 355/355 通过，`git diff --check` 通过。
- 2026-08-07：仓库无批准弹药资源；按 art pipeline 门禁保留 `Ammo9mm.visualAssetsPublished=false`，不加载空路径、不生成正式内容、不写入临时占位图。RL-ART-002 在独立美术生产任务批准发布后关闭。
- 2026-08-07：M3 C++ 安全审查未发现代码阻塞项：计划 vector、split 实例和目标 placement 容量均在 mutation 前完成分配；提交阶段只按稳定 ID 重新查询，不跨 reserve/remove 保存业务引用；失败返回不消耗 ID，提交后不可能失败的内部不变量破坏采用 fail-fast。
- 2026-08-07：独立美术任务 `019fdb3a-add1-7ab3-a67e-8cd0ad4bc009` 完成 2 个候选和唯一 1 次修复。主控经原图与 64×64/32×32 审查批准 candidate 01；确定性派生、透明边缘、六像素安全留白、背包/地图预览和机器 QA 全部通过。正式合同只要求“少量弹药可见”，因此记录并接受未严格呈现四枚的视觉偏差；`Ammo9mm` 正式开启纹理发布。
- 2026-08-07：正式场景新增数量 25 与 40 的两堆 9mm 弹药，保留原有四件地面物品的稳定 ID 顺序，为拾取合并、部分转移、整栈丢弃和角标提供真实窗口入口。发布后主程序/`GameplayWorldTest` 定向构建通过，`GameplayWorldTest.exe` 48/48、Windows Debug 全量 CTest 355/355 通过，运行时目录包含 64×64 与 32×32 弹药纹理；M3 进入 1–8 项人工验收。
- 2026-08-07：用户确认 M3 第一版真实窗口 1–8 项全部通过，随后修订数量操作语义：Ctrl/Shift+左键改为按数量拿起并拖拽，且 PlayerOnly 也必须可用；F/Ctrl+右键仍是整栈快速转移。
- 2026-08-07：新增 `tryPlaceItemQuantityAt` 精确格事务、同背包拆分/合并、跨容器指定格数量放置、部分脚下丢弃与数量虚影；四个受影响 Debug 测试程序直接运行共 127/127 通过且无运行库错误，全目标增量构建与 Windows Debug CTest 367/367 通过，`git diff --check` 通过。
- 2026-08-07：用户确认按数量拖拽修订版真实窗口 1–8 项全部通过。M1、M2、M3 的本地自动化、资源 QA、安全审查和人工门禁均完成，进入两提交、单推送、单 PR/CI 的最终收口。

## 发现记录

- Week18 合并后，静态状态文档仍保留 CI pending/旧 main SHA；真实 Git 与 PR #33 是当前权威证据。
- 当前 Idle 状态已经保留 `hoveredLocation`，因此快捷转移无需引入持久 selection。
- 当前 `InventoryPointerEvent` 只有左键，`InputSystem` 也没有独立 Ctrl 状态；Ctrl+右键必须扩展规范化输入而不能直接在业务模型读取 SDL。
- 当前 first-fit 跨容器事务已经满足 M1 所需所有权与回滚语义，M1 不需要改 `GridInventory`。
- `F` 若只复用上一帧 hover，刚打开界面且鼠标未移动时会漏掉目标；实现改为在 F key-down 事件发生时采样当前鼠标位置，再由同一 UI 队列按事件顺序解析 hover。
- 快捷转移只保留格位置和稳定 ID，容器 mutation 前后不保存 `PlacedItem` 引用、迭代器或下标；现有 remove/place 事务继续负责失败回滚。所有权与失效审查未发现 M1 阻塞项。
- SDL 窗口失焦时若平台未产生 Ctrl key-up，现有原始按键集合可能保留按下状态；该风险与已有移动键的失焦粘键风险同类，不影响本阶段正常事件序列，留待输入系统生命周期专项处理。
- 拆分堆叠会首次要求在现有实例之外创建新稳定 ID，是 M3 的主要所有权风险。
- M2 扩大 `InventoryInteractionState` 后，增量 `MouseInventoryInteractionTest` 再次出现 `gtest_ar_` 周围栈损坏；同一构建目录还出现旧 `app.cpp.obj` 引用早期函数签名。Ninja `-t deps` 证明相关对象为 `#deps 0`，根因是 CMake 3.31 对中文 MSVC `/showIncludes` 前缀的编码与后续编译代码页不一致。
- `CMakeLists.txt` 现在只在检测到已知乱码前缀时纠正它；本机构建流程固定 UTF-8 代码页并 fresh configure。最终 `app.cpp.obj` 记录 191 个依赖、鼠标测试对象记录 150 个依赖，二者都明确包含 `inventory_interaction.h`，随后干净构建与 335/335 通过。
- C++ 所有权审查确认：orientation 提交前完成边界/碰撞/方向验证和目标容量预留；提交后不跨 mutation 使用旧 vector 引用；失败路径恢复源方向与 origin，未发现 M2 阻塞项。

## 最终结果与遗留问题

Week19 的本地实现、自动测试、安全审查、资源 QA 和全部真实窗口验收已完成。当前唯一未完成的计划证据是冻结提交对应的 Windows/Ubuntu CI 与合入；精确 SHA、run URL 和结论将写入 PR，不在 CI 后追写仓库文档。计划在远端门禁完成前保持 `In Progress`。

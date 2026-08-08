# Project Raidline 问题台账

最后核对：2026-08-08。本文是当前问题、风险和延期债务的仓库内权威记录；GitHub Issue 用于跟踪可执行项，源码、测试和 CI 仍是行为事实来源。

## 状态约定

- `Open`：已确认，需要后续处理。
- `Needs Decision`：现象已确认，但产品行为或资源方案尚未冻结。
- `Local Fixed`：本地实现和目标测试已完成，等待 CI 或人工验收后关闭。
- `Closed`：关闭条件已有自动、CI 和适用的人工证据。
- `Monitoring`：当前不可复现，保留触发条件和复现要求。
- `Deferred`：已接受但不阻塞当前里程碑。

## 当前问题

| ID | 严重度 | 领域 | 问题与证据 | 状态 | 目标与关闭条件 |
| --- | --- | --- | --- | --- | --- |
| RL-W17-001 | P1 | 背包输入 | SDL 事件轮询中曾直接处理 mouse-up，而 Esc/Tab 到 `update()` 才处理；同帧取消可能晚于 `tryMove`。 | Closed | 29/29 鼠标测试覆盖同帧 Esc/Tab + release；Windows/Ubuntu CI 与 2026-08-07 真实窗口取消验收通过。 |
| [RL-W17-002 / GitHub #25](https://github.com/Disthinker/Project-Raidline/issues/25) | P1 | 交付 | Week17 已由 PR #31 跟踪；自动测试、Windows/Ubuntu CI 和 9 项真实窗口验收均已有记录。 | Closed | 2026-08-07 完成；后续 UX/动画/架构债由 #26–#30 独立跟踪。 |
| [RL-UX-001 / GitHub #26](https://github.com/Disthinker/Project-Raidline/issues/26) | P2 | 背包 UX | 产品决策已冻结为纯鼠标背包；Week18 已删除方向键/Enter 库存动作、键盘状态、黄色焦点与提示，输入与真实窗口均确认这些键不再具有库存语义。 | Closed | 2026-08-07 经 PR #33 的 Windows/Ubuntu CI 验证并合入 `main@7e436aa`。 |
| [RL-UX-002 / GitHub #27](https://github.com/Disthinker/Project-Raidline/issues/27) | P2 | 背包 UX | 平滑拖拽虚像已由 PR #32 合入 `main` 的 `c6cda7b`；连续像素虚像与吸附候选保持分离。 | Closed | 2026-08-07 完成；Week18 双容器继续复用并扩展该契约。 |
| RL-W18-001 | P1 | 容器与丢弃 UX | 首轮 Week18 人工验收后发现第二容器无世界实体/交互门控、丢弃区未贴右侧、Idle 残留蓝色框，随后追加丢弃物应落在角色脚下。柜体实体与近距离 F 门控、贴右半透明长条、release/Esc/Tab 清选择及脚下落点均已完成。 | Closed | 用户确认修订版 12–16 全部通过；2026-08-07 经 PR #33 CI 验证并合入 `main@7e436aa`。 |
| RL-W26-001 | P2 | 瞄准 UX | Week26 原 1–10 人工验收通过后发现代码准星与系统鼠标同时显示。App 已在活动 Raid、库存关闭且准星可用时隐藏系统鼠标，并在菜单、Base、库存、终局和 shutdown 恢复。 | Local Fixed | 补充真实窗口验收确认无双鼠标、跨 UI 显隐正确；精确 feature head Windows/Ubuntu CI 通过后关闭。 |
| RL-W26-002 | P2 | 投射物表现 | 白色方块与 700 px/s 原型先改为 900 px/s 火光拖尾，再按反馈缩为 3×3 热芯与两段细线。最新反馈要求进一步缩短飞行时间并突出命中；本地已提高到 1200 px/s，以最多 8 px 常规步长解析高速命中，并把灰色粒子改为 16 枚短寿命白热/金黄/红橙火花。逻辑 AABB 仍为 8×8、伤害仍为 1。 | Local Fixed | 最终真实窗口验收确认弹头细小、拖尾清晰、飞行短促且火光命中反馈醒目；全量测试与精确 feature head Windows/Ubuntu CI 通过后关闭。 |
| [RL-INV-001 / GitHub #38](https://github.com/Disthinker/Project-Raidline/issues/38) | P2 | 背包交换 | 拖动物品覆盖已有物品时，如果被拖动物与目标处一个或多个完整 placement 可以合法互换区域，则预览应显示绿色并原子交换；稳定 ID、定义、数量与方向保持不变，任一物品无法安置时全部不变。 | Open | 后续库存 UX ExecPlan 冻结单/多物品确定性布局，覆盖旋转 footprint、非法交换与 Esc/Tab 同帧取消后实现。 |
| [RL-INV-002 / GitHub #39](https://github.com/Disthinker/Project-Raidline/issues/39) | P2 | 数量拖拽 UX | Ctrl/Shift+左键选择 1 个/一半堆叠后应进入点击锁定：松开鼠标和修饰键仍让数量虚像跟随，下一次普通点击合法目标才提交；当前实现仍采用按住左键并在 release 提交。 | Open | 后续库存 UX 状态机任务实现 click-to-pick/click-to-place，保持 Esc/Tab 取消、非法目标不变、PlayerOnly/双容器一致和 Ctrl+Shift 无操作。 |
| [RL-ANIM-001 / GitHub #28](https://github.com/Disthinker/Project-Raidline/issues/28) | P2 | 角色表现 | 仓库只有左右移动图集；纯上/下移动回退到同一静态贴图，停止后虽保留模型朝向但视觉上恢复默认。 | Needs Decision | 角色动画任务；冻结四方向资源方案，补上/下动画与停止朝向验收后关闭。 |
| [RL-ARCH-001 / GitHub #29](https://github.com/Disthinker/Project-Raidline/issues/29) | P2 | 架构 | `src/app.cpp` 仍集中 SDL 生命周期、输入、纹理和背包编排；Week18 已用容器 ID、容器查询与独立布局接口解除“只能绑定玩家背包”的限制。 | Deferred | 多容器阻塞已解除；后续仅在玩法需求驱动下继续拆分，不做无关大重构。 |
| [RL-BUILD-001 / GitHub #30](https://github.com/Disthinker/Project-Raidline/issues/30) | P2 | 构建 | 多个测试 target 重复编译业务源码，扩大干净构建成本和 target 接线遗漏风险；本轮旧 ABI 对象的直接根因已拆分为 RL-BUILD-002。 | Deferred | 后续抽取共享核心 library，并证明所有测试链接同一实现；不阻塞 M2 人工验收。 |
| RL-BUILD-002 | P1 | Windows 构建 | Week19 M2 再次出现 `MouseInventoryInteractionTest.exe` 的 `gtest_ar_` 栈损坏，主程序随后也链接到旧函数签名。Ninja `-t deps` 显示相关对象为 `#deps 0`；根因是 CMake 3.31 中文 MSVC `/showIncludes` 前缀编码与编译代码页不一致。 | Closed | CMake 纠正已知乱码前缀；固定 UTF-8 后干净/增量构建与全量测试均通过，PR #34 Windows CI 通过。Week22 新增类与 target 后 RaidSettlementTest 直接运行 12/12、全量 434/434、PR #40 Windows CI 再次未复现。 |
| RL-CI-001 | P3 | CI | `tests/test_phase1_assets.py` 未注册进 CTest 或 GitHub Actions。 | Deferred | 在独立 CI 改进任务中接入并保留失败诊断。 |
| RL-DATA-001 | P3 | 数据 | ItemDefinition、世界参数、射击参数和部分 UI 布局仍为编译期硬编码。 | Deferred | 数据驱动里程碑排期后处理，不阻塞 Week18。 |
| RL-ART-001 | P3 | 美术管线 | 已批准资源的覆盖保护主要依赖流程，跨包候选扫描和部分命名枪械工具仍硬编码。 | Deferred | 后续艺术管线加固任务。 |
| RL-ART-002 | P1 | Week19 弹药资源 | 独立美术任务 `019fdb3a-add1-7ab3-a67e-8cd0ad4bc009` 已完成 2 个候选和唯一一次修复；主控批准 candidate 01，正式 source/inventory/world 资源、manifest、QA 与验收记录均已发布，`Ammo9mm.visualAssetsPublished=true`。精确四枚构图未满足，但正式合同只要求少量弹药可见，该偏差已登记。 | Closed | 数量拖拽修订版人工验收和 PR #34 Windows/Ubuntu CI 均通过。批准身份不得由后续静默生成覆盖。 |

## 历史与监控

| ID | 现象 | 当前结论 | 再次出现时的要求 |
| --- | --- | --- | --- |
| RL-MON-001 | MSVC Debug 曾报告 `gtest_ar_` 周围栈损坏。 | Week19 已形成可核验证据并升级为 RL-BUILD-002：Ninja 对关键对象记录 `#deps 0`，导致类布局变化后调用方未重编译。 | 后续同类现象统一按 RL-BUILD-002 检查代码页、`ninja -t deps` 与干净构建证据。 |
| RL-MON-002 | 普通 PowerShell 中直接增量构建找不到 `<optional>`、`<array>` 等标准库。 | 未加载 Visual Studio Developer Shell，属于工具链环境问题；旧 CTest 二进制不能作为新代码证据。 | 先运行 `Launch-VsDevShell.ps1` 或使用 Developer PowerShell，再重新 build 后运行测试。 |

## 分级规则

- P0/P1 阻塞当前里程碑关闭和下一功能分支。
- P2 只有在影响当前正确性、复现频率高或直接阻塞下一里程碑时升级为阻塞项。
- P3 进入明确的工程改进任务，不夹带进相邻玩法功能。
- `Local Fixed` 只有在精确提交的 CI 与适用的人工验收通过后才能改为关闭。

# Project Raidline 问题台账

最后核对：2026-08-06。本文是当前问题、风险和延期债务的仓库内权威记录；GitHub Issue 用于跟踪可执行项，源码、测试和 CI 仍是行为事实来源。

## 状态约定

- `Open`：已确认，需要后续处理。
- `Needs Decision`：现象已确认，但产品行为或资源方案尚未冻结。
- `Local Fixed`：本地实现和目标测试已完成，等待 CI 或人工验收后关闭。
- `Monitoring`：当前不可复现，保留触发条件和复现要求。
- `Deferred`：已接受但不阻塞当前里程碑。

## 当前问题

| ID | 严重度 | 领域 | 问题与证据 | 状态 | 目标与关闭条件 |
| --- | --- | --- | --- | --- | --- |
| RL-W17-001 | P1 | 背包输入 | SDL 事件轮询中曾直接处理 mouse-up，而 Esc/Tab 到 `update()` 才处理；同帧取消可能晚于 `tryMove`。 | Local Fixed | Week17；同帧 Esc/Tab + release 自动测试、Windows/Ubuntu CI 和真实窗口验收通过后关闭。 |
| [RL-W17-002 / GitHub #25](https://github.com/Disthinker/Project-Raidline/issues/25) | P1 | 交付 | Week17 最终代码尚缺对应提交的双平台 CI 和 9 项真实窗口验收。 | Open | Week17；PR CI 全绿且人工清单有实测记录后关闭。 |
| [RL-UX-001 / GitHub #26](https://github.com/Disthinker/Project-Raidline/issues/26) | P2 | 背包 UX | 用户认为方向键移动物品、黄色焦点框和相关提示是遗留行为；当前批准的 Week17 契约仍要求保留 Week16 键盘兼容。 | Needs Decision | 后续稳定化；决定“纯鼠标”或“键鼠并存”，同步不变量、测试和提示后关闭。 |
| [RL-UX-002 / GitHub #27](https://github.com/Disthinker/Project-Raidline/issues/27) | P2 | 背包 UX | 鼠标拖拽虚像当前按候选格吸附跳动；用户期望虚像按像素平滑跟随鼠标，落点合法性仍吸附格子。 | Open | 后续稳定化；虚像保持抓取点平滑移动，inside/outside release 与事务规则不变，视觉验收通过后关闭。 |
| [RL-ANIM-001 / GitHub #28](https://github.com/Disthinker/Project-Raidline/issues/28) | P2 | 角色表现 | 仓库只有左右移动图集；纯上/下移动回退到同一静态贴图，停止后虽保留模型朝向但视觉上恢复默认。 | Needs Decision | 角色动画任务；冻结四方向资源方案，补上/下动画与停止朝向验收后关闭。 |
| [RL-ARCH-001 / GitHub #29](https://github.com/Disthinker/Project-Raidline/issues/29) | P2 | 架构 | `src/app.cpp` 集中 SDL 生命周期、输入、纹理和背包编排，并直接绑定玩家背包。 | Deferred | Week18 仅提取双容器所需的最小库存 UI 编排接口；不做无关大重构。 |
| [RL-BUILD-001 / GitHub #30](https://github.com/Disthinker/Project-Raidline/issues/30) | P2 | 构建 | 多个测试 target 重复编译业务源码；类布局变化时旧对象可能产生 ABI 尺寸不一致。 | Deferred | 后续抽取共享核心 library，并证明所有测试链接同一实现。若栈损坏复现则提升为 P1。 |
| RL-CI-001 | P3 | CI | `tests/test_phase1_assets.py` 未注册进 CTest 或 GitHub Actions。 | Deferred | 在独立 CI 改进任务中接入并保留失败诊断。 |
| RL-DATA-001 | P3 | 数据 | ItemDefinition、世界参数、射击参数和部分 UI 布局仍为编译期硬编码。 | Deferred | 数据驱动里程碑排期后处理，不阻塞 Week18。 |
| RL-ART-001 | P3 | 美术管线 | 已批准资源的覆盖保护主要依赖流程，跨包候选扫描和部分命名枪械工具仍硬编码。 | Deferred | 后续艺术管线加固任务。 |

## 历史与监控

| ID | 现象 | 当前结论 | 再次出现时的要求 |
| --- | --- | --- | --- |
| RL-MON-001 | MSVC Debug 曾报告 `gtest_ar_` 周围栈损坏。 | 对相关 target 干净重建后不再复现；现有证据指向类布局变化后的新旧对象混用，但未形成独立最小复现。 | 保存准确 executable、调用栈、构建命令和对象时间戳；立即干净重建并比较结果。 |
| RL-MON-002 | 普通 PowerShell 中直接增量构建找不到 `<optional>`、`<array>` 等标准库。 | 未加载 Visual Studio Developer Shell，属于工具链环境问题；旧 CTest 二进制不能作为新代码证据。 | 先运行 `Launch-VsDevShell.ps1` 或使用 Developer PowerShell，再重新 build 后运行测试。 |

## 分级规则

- P0/P1 阻塞当前里程碑关闭和下一功能分支。
- P2 只有在影响当前正确性、复现频率高或直接阻塞下一里程碑时升级为阻塞项。
- P3 进入明确的工程改进任务，不夹带进相邻玩法功能。
- `Local Fixed` 只有在精确提交的 CI 与适用的人工验收通过后才能改为关闭。

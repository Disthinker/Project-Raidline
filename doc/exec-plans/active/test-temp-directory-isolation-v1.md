# 测试临时目录隔离 v1（修复回归通过，整体待CI）

**用户已批准先修复测试夹具的临时目录分配，射击模式功能暂不实施。** PR197已经按用户授权合并；main两轮Windows检查均在测试准备阶段因目录已存在退出，游戏UI断言尚未执行。Ubuntu均2882/2882，七项3ms检查在两轮均通过。第三轮不再重试。

- plan_version：`RL-TEST-TEMP-DIR-ISOLATION-v1`；task_id：`test-temp-directory-isolation-v1`；state：`scoped_verified_full_perf_failed_pending_ci`；approval_source：2026-10-28主控原生问题回复“同意，先修测试夹具（推荐）”，对应本版本完整问题，授权本地检查后提交、推送，准确HEAD双平台CI通过后合并测试修复；不实施射击模式。
- 基线：main `80d39c4b50860e598b5367ca2df0fcadb4f5b00e`。从此基线建立独立分支`codex/test-temp-dir-isolation-v1`，实施批准范围内测试修复；生产代码、CI配置和门槛保持。

## 已确认的问题

第一次：`UiBaseDeploymentTest.SelectedIntelligenceChangeWithoutRevisionCancelsHeldAction`在`tests/test_ui_base_deployment.cpp:106`的SetUp调用`create_directory(path)`返回false。

第二次：`UiQuickWheelTest.CacheRefreshKeepsOriginalInstanceOrderAndCorrectCountMeaning`在`tests/test_ui_quick_wheel.cpp:90`以同样原因失败。两处都使用“固定前缀＋steady_clock时间戳”作为共享临时目录名，随后假定目录一定新建成功；该命名没有跨进程唯一性保证。已确认目录已存在，失败发生在App创建和测试主体之前；日志未记录具体目录或占用者，不能断言是哪一个进程创建了它。命名与清理缺少唯一性／所有权保护是可复现的风险来源。

已逐文件核查53个命中：16个UI夹具与本次两处失败完全同形，首批仅修这16处；2个已有独占重试保护，保持不动；其余35个不同形风险仅记录，不纳入本次修复。具体范围见下方清单，不将整个tests目录作为可任意改动范围。

## 具体修什么

1. **提供一个仅测试使用的独占目录分配器。** 拟新增`tests/test_temp_directory.h`。参照已有tests/test_raid_light_cone_visibility_ui.cpp的独占认领重试策略，结合tests/test_ui_item_context.cpp已有ownsDirectory保护；这两处安全实现保持不动，新的共用helper只接入以下16处；继续放在系统临时目录，采用固定测试前缀／测试名、时间戳与候选序号，并以`create_directory`原子创建成功作为取得所有权的唯一条件。遇到已存在路径换新候选、有限重试；权限、磁盘等真实I/O错误仍明确失败，不能吞掉错误或接管已有目录。
2. **统一不安全夹具的分配与清理。** 范围固定为上述两处已复现夹具及其余14处完全同形消费者，共16个现有文件；保留测试主体和全部功能断言。只有创建成功的独占目录交给本夹具清理，初始化失败不得清理其他测试的路径。含性能计时的夹具只调整计时外的临时路径准备，原计时、样本、负载及阈值保持。
3. **增加有针对性的隔离回归。** 拟新增`tests/test_temp_directory.cpp`并登记到CMake：预置同名路径、并发创建、所有权清理和真实I/O错误。验证新路径互不相同，碰撞路径及其他测试资料保持，错误不会被误报为通过。
4. **重新验证准确提交。** 跑已失败两组、相关夹具并行回归、原2882项及新增回归；Windows／Ubuntu仍按原并行策略执行。七项性能检查继续使用原负载、分位与3ms门槛，不通过跳过测试或全局串行化来绕过冲突。

## 影响与交付

这是测试基础设施修复，范围限`tests/`和必要的测试登记；游戏生产逻辑、产品界面、存档schema／内容、玩家文件、固定程序入口及玩法均保持。本修复不启动UI专项，也不夹带“武器半／全自动模式切换”的实现。

用户本轮已明确授权：从当前main开独立测试修复分支`codex/test-temp-dir-isolation-v1`；本地检查后提交、推送并建独立PR，验证准确HEAD的双平台CI，通过后合并该测试修复。纯测试维护以隔离回归和完整CI验收，不额外要求重复游玩；不启动游戏，不自动开始下一功能。

下一功能[武器半／全自动模式切换v1](weapon-fire-modes-v1.md)保留为独立待批准方案；其实现仍需要另有明确批准。当前正式美术／音频、信任／权限／模型均不更改。

<details>
<summary>证据与保全</summary>

### 首批16个现有夹具

- `tests/test_ui_base_allocation.cpp`
- `tests/test_ui_base_deployment.cpp`
- `tests/test_ui_base_dormitory.cpp`
- `tests/test_ui_base_management.cpp`
- `tests/test_ui_base_medical.cpp`
- `tests/test_ui_base_supply.cpp`
- `tests/test_ui_base_workshop.cpp`
- `tests/test_ui_final_consolidation.cpp`
- `tests/test_ui_final_containers.cpp`
- `tests/test_ui_item_details.cpp`
- `tests/test_ui_lost_recovery.cpp`
- `tests/test_ui_quick_wheel.cpp`
- `tests/test_ui_raid_hud.cpp`
- `tests/test_ui_raid_result.cpp`
- `tests/test_ui_regional_operations.cpp`
- `tests/test_ui_weapon_components.cpp`

除此仅拟新增共用header、聚焦回归文件及必要测试登记；35个其他风险文件和2个已有保护文件均不在本修复范围。分类行号与源码哈希见`merge/temp-dir-inventory.json`。

- main CI run40659086393，event=push，准确HEAD80d39c4。attempt1 Win job122717348714，attempt2 Win job122736579062；两次各2881/2882且失败位置不同，均为SetUp目录创建。Ubuntu对应检查均2882/2882；首轮失败未被第二轮覆盖。
- 证据目录：`C:\Users\Administrator\Documents\美术资源生成\tmp\raid-runner-infected-ci-v2-20261028\merge`；`main-attempt-1/`、`main-attempt-2/`保存各轮日志／元数据／逐名证明。根目录`main-retry-*`为第二轮原始输入，旧`main-*`为首轮原始输入；任何新修复须采用新提交与新CI证明，不能把旧通过结果贴到新树。
- 批准前源码599份匹配验收候选b9344c8cd631；批准后仅测试文件与必要登记进入修复。固定程序237c04…、21份玩家文件及其他未提交工作保全快照见本次证据目录。没有运行游戏或重试旧失败CI。
- 回退仅涉及测试目录工具与接线，按独立提交恢复；不需要回退玩家存档或替换游戏程序。

</details>

## 本次执行记录

- 证据目录：`C:\Users\Administrator\Documents\美术资源生成\tmp\test-temp-dir-isolation-v1-20261028`；`before.json`及`original-work/`、`player-backup/`保留本轮开始时五目录脏文件与21份最新玩家文件。
- 固定程序与入口保持；本次无需玩家重复游玩，采用新增隔离回归、原完整测试及准确提交双平台CI完成测试维护验收。
- 本地检查：windows-debug已构建17个受影响测试目标；新增12/12、关联395/395通过。全量已完整执行，为2893/2894，0跳过；唯一既有混合敌群性能100目标p95=3.555ms，原参数定向复测3.4019ms仍未过，停止继续重试。原2882名称保留、新增12项，并行4及七项3ms门槛不变；本地整体尚未通过。详见`local-proof.json`、XML与日志。用户已允许提交推送验证CI；如准确HEAD两平台任一项未过，保留阻塞、不合并。

### 实现与验证边界

- `tests/test_temp_directory.h`只在测试中使用：原子创建成功才取得所有权；按测试名／时间戳和候选序号重试，真实I/O错误明确失败。清理校验捕获的TEMP根、前缀和目录类型，只处理成功认领路径；消费者先析构，失败SetUp也安全。
- 16个原测试主体逐字对照保持；仅改准备与清理，三处detach增加空App检查。其余37个清单文件保持，包括35处另形风险和2处既有安全实现；没有假称已修整个测试目录。
- 新增12项包括同seed的8线程并发、文件／目录碰撞哨兵、有限耗尽、真实I/O错误、清理互不影响及路径边界。必要CMake登记2行，保持原CI并行和所有性能负载／计时／断言。
- 本次唯一失败的性能路径及其生产源码与基线保持；本轮构建未重建该性能目标。不能把失败归因于具体环境或进程，也不据此修改或放宽性能要求。已完成必要全量执行并保留失败，按已批准的先提交推送验证CI流程继续；准确双平台全部通过才合并，不以局部通过替代整体门槛。
- 生产411份文件、已验收程序、固定入口、21份玩家文件和范围外脏文件按本轮快照核对保持；不启动游戏。测试修复以自动化交付，不要求重复玩法验收。

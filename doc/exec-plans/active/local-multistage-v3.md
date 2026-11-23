# 四小时多阶段玩法开发

本轮在 2026-11-23 北京时间 11:29 至 15:29 本地迭代。用户批准连续实施 `RL-LOCAL-MULTISTAGE-v3`，四项完成后可继续向原策划线程领取下一批功能。主线基线为 `ad3268c9eb8520f9049e7a3665f808537b72d240`。

| 顺序 | 玩家要做什么 | 应看到的结果 |
| --- | --- | --- |
| 1 | 从基地普通出击进入医院，在主楼与救护车保障站之间往返 | 两处分别提供医疗物资和工坊材料。返回正确门口，已搜物资、击倒的敌人及每栋建筑的地图知识各自保留。 |
| 2 | 装工具刀，点 V 或按住 Alt + V | 轻击立即起手。重击可在准备期间取消，完成重击后收招更久，每次命中只结算一次。 |
| 3 | 去基地工坊，选择仓库中闲置的空枪拆解 | 确认后获得工坊零件，未卸附件、带弹或不属于玩家的枪械不能拆。 |
| 4 | 在工坊用材料生产声响诱饵 | 制成品可带入下一局并用于引开敌人。 |

## 本地验证

各阶段先验证普通入口与跨存档结果，再冻结可运行的本地程序。联合候选使用独立测试档。Windows 现有 14 项 3 ms 检查及基地存档 8 ms 检查采用原负载和门槛。此前主线 Windows 存档测试 P95 为 8.0073 ms，Ubuntu 通过。[主线 CI](https://github.com/Disthinker/Project-Raidline/actions/runs/44705517375)

策划回执为 `MAIN-PLANNER-MULTISTAGE-20261123-03`，任务 `raid-four-hour-local-iteration`，协议 `RL-LOCAL-MULTISTAGE-v3`。构建、回归和产物指纹由主控在 [本机验证目录](<E:/RaidlineBuild/verification/local-multistage-v3>) 归档。

### 已验证的阶段

- **医院双地标。** 87 项专项通过，包括 128 个种子的布局和预算、两座建筑往返、战利品撤离、跨进程回读及旧单建筑存档。保障站的材料和普通感染者从既有预算调入。阶段程序指纹为 `c6673429d84c`。[验证记录](<E:/RaidlineBuild/verification/local-multistage-v3/hospital-final.log>)
- **近战轻重攻击。** 92 项专项通过。重击准备 0.45 秒，收招 1 秒，单次伤害与护甲损耗均为轻击的两倍。中文蓄力条、松键取消、持键只挥一次、进门及失焦取消均有回归。阶段程序指纹为 `7598acb95c9f`。[验证记录](<E:/RaidlineBuild/verification/local-multistage-v3/melee-release.log>)
- **基地废枪拆解。** 65 项专项通过。工坊的「枪械拆解」按仓库实例选枪，确认后换取一件占两格的废旧零件。仅有竖向空间时会旋转零件，无空间或存档写入失败时保枪。三种窗口大小的确认界面已留图。阶段程序指纹为 `cd7d9ae7b1e5`。[验证记录](<E:/RaidlineBuild/verification/local-multistage-v3/salvage-official.log>)
- **诱饵制作。** 工坊用一份废旧零件和一卷铜线生产两枚声响诱饵，基准工时 90 分钟。75 项专项及一项补充读档验证通过。实际对局测试从保障站带回铜线，在基地拆枪制造，再去边境交换站投出一枚，撤离并读回余下一枚。四项联合候选 `b5075864a284`。[联合流程](<E:/RaidlineBuild/verification/local-multistage-v3/decoy-chain.log>) · [工单回读](<E:/RaidlineBuild/verification/local-multistage-v3/decoy-workorder-save.log>)

首批四项的 Windows 完整回归 3453/3453 通过，用时 370.84 秒，包含原性能门槛。[完整验证](<E:/RaidlineBuild/verification/local-multistage-v3/initial-four-full.log>)

## 同一窗口内的第二批玩法

用户授权四项提前完成后由原策划线程继续排期并执行。策划回执 `MAIN-PLANNER-CONTINUOUS-20261123-04` 对应任务 `raid-continuous-multistage`、方案 `RL-LOCAL-MULTISTAGE-v4`，基线为主线 `ad3268c` 加首批本地候选 `b5075864a284`。

| 顺序 | 功能 | 玩家可验证的结果 |
| --- | --- | --- |
| 5 | 活队友定向治疗 | 靠近受伤队友，使用随身医疗包。治疗完成才扣一次，离开或失去视线会中断。 |
| 6 | 河畔商人物物交换 | 在河畔用携带的物资换弹药或战术消耗品。每种交易每局限一次，带出仍需撤离。 |
| 7 | 基地医疗品余量整合 | 在基地把两件同型号、未用完的医疗品合并，保留总剂量，腾出空间。 |
| 8 | 医院幸存者街区护送 | 在普通医院对局中找到幸存者，护送至交接点，成功后才接入基地安置。 |

**队友治疗已冻结**，程序 `df801a4d67c3`。实际用例从出击借装进入医院，验证治疗时停止跟随与射击、完成扣药、失去视线或受控取消、指令恢复，以及玩家原有自疗。F2 面板的鼠标和键盘输入经过 SDL 测试。86/86 项专项通过。[回归记录](<E:/RaidlineBuild/verification/local-multistage-v3/treatment-official.log>) · [中文面板](<E:/RaidlineBuild/verification/local-multistage-v3/treatment-panel-final-images/treatment-1280-zh.png>)

**河畔物物交换已冻结**，程序 `6f5c61093725`。62/62 项专项通过。交易、撤离、读档、装弹及下一局实际投掷在同一用例串行完成。仓库材料、敌情变化、陈旧报价和重复兑换均有拒绝回归。随包存档另经正常出击流程验证，医院能带队友进入两座地标，河畔能生成既有联络点。[回归记录](<E:/RaidlineBuild/verification/local-multistage-v3/barter-official.log>) · [中文联络点](<E:/RaidlineBuild/verification/local-multistage-v3/barter-panel-images/riverside-barter-1280-zh.png>)

**医疗品余量整合已冻结**，程序 `ee8042c15e8a`。82/82 项专项通过，覆盖读档、失败重试、重复确认和个人药品所有权。从随包存档进入基地，把余量 1/3 和 2/3 的医疗包合并，实际将原接收包带入下一场医院出击，队友治疗菜单能选到它。[验证记录](<E:/RaidlineBuild/verification/local-multistage-v3/medical-merge-official.log>) · [中文界面](<E:/RaidlineBuild/verification/local-multistage-v3/medical-consolidation-panel-images/medical-consolidation-1280-zh.png>)

**医院街区护送已冻结**，八项联合程序 `64f3cc50a2dd`。128 个医院种子的护送起点处于交接区中心 600 至 1000 单位外，生成时按角色宽度找一条可走的街道。47 项相关回归通过，实际交接画面另核对了满床警告和口粮。改动前保存的本地医院待局仍按单点营救读取，重新序列化逐字节一致。[联合回归](<E:/RaidlineBuild/verification/local-multistage-v3/escort-combined-release.log>) · [旧档及随包出击](<E:/RaidlineBuild/verification/local-multistage-v3/escort-package-probe.log>) · [交接画面](<E:/RaidlineBuild/verification/local-multistage-v3/escort-render-and-access-images/hospital-escort-handoff-1280-zh.png>)

八项累计 Windows 全量通过 3478/3478。千件资产的基地存档 P95 为 4.4318 ms，14 项 3 ms 门槛均沿用原负载。[全量记录](<E:/RaidlineBuild/verification/local-multistage-v3/eight-stage-full.xml>)

## 第三批战术玩法

原策划回执 `MAIN-PLANNER-CONTINUOUS-20261123-05` 对应 `raid-continuous-multistage / RL-LOCAL-MULTISTAGE-v5`，基线为 `ad3268c` 加本地 `64f3cc50a2dd`。四项依次为普通出击的烟雾掩护、医院与边境的可引爆油桶、短时火区和一次性警戒器。

**烟雾掩护已冻结**，程序 `c36a2727f757`。基地购买、装胸挂、投掷与撤离读盘均走正常服务；烟幕遮住敌我视线，飞行中的子弹与移动照常。81/81 项专项通过，另以游戏纹理复核烟幕两侧的实际显示。[回归记录](<E:/RaidlineBuild/verification/local-multistage-v3/smoke-official.log>) · [投掷画面](<E:/RaidlineBuild/verification/local-multistage-v3/smoke-art-evidence-images/smoke-cover-1280-zh.png>)

**后勤区油桶已冻结**，程序 `9f0474260f06`。医院和边境交换站各检查128个种子，确认两处桶位避开出生、门口和撤离点。33/33项专项通过；另以中文出击页进入医院，实弹击中、玩家受伤与残骸显示均通过。旧局缺字段时保持无桶；使用独立试玩档实走医院、河畔出击，旧医院快照逐字节相同。[回归记录](<E:/RaidlineBuild/verification/local-multistage-v3/barrels-official.log>) · [随包检查](<E:/RaidlineBuild/verification/local-multistage-v3/barrels-package-probe.log>)

**工坊火区委托已冻结**，程序 `ad03ab35b413`。74/74 项专项通过。工坊真实领用成品进入胸挂；投掷后视野与受击在墙两侧一致，离开火区立即停止伤害，双火区重叠保持同一伤害节拍。[回归记录](<E:/RaidlineBuild/verification/local-multistage-v3/fire-official.log>) · [火区画面](<E:/RaidlineBuild/verification/local-multistage-v3/fire-ui-final-images/fire-zone-1280-zh.png>)

**警戒器与中断恢复已冻结**，程序 `7b0a75063da5`。67/67项专项通过。在医院同一局从随包胸挂使用警戒器、烟雾和火区，两个油桶均已生成；医院和河畔出击保留原地图与队友权限。额外用真实旧医院对局中断档经「继续」回基地后重新出击，原档仍保持逐字节相同。旧部署事务保留在账本，新尝试使用独立身份。[连续两次恢复后再出击](<E:/RaidlineBuild/verification/local-multistage-v3/twelve-repeat-crash.log>) · [专项结果](<E:/RaidlineBuild/verification/local-multistage-v3/alarm-release.log>) · [联合实走](<E:/RaidlineBuild/verification/local-multistage-v3/twelve-old-recovery-and-play.log>) · [中文提示](<E:/RaidlineBuild/verification/local-multistage-v3/alarm-visual-images/alarm-warning-1280-zh.png>)

十二项 Windows 全量最终通过 **3517/3517**，耗时 352.10 秒。最初四处失败来自旧版快照夹具和发布目录新增内容的预期，保留旧版完整比对并补核新物品、工坊第三页及新油桶。原 14 项 3 ms 负载通过，千件资产真实磁盘存档检查 P95 **4.4204 ms**（门槛 8 ms）。[全量报告](<E:/RaidlineBuild/verification/local-multistage-v3/twelve-stage-final.xml>)

## 第四批 · 经营与外勤探索

原策划回执 `MAIN-PLANNER-CONTINUOUS-20261123-06`，任务 `raid-continuous-multistage`，版本 `RL-LOCAL-MULTISTAGE-v6`，基线 `ad3268c9eb8520f9049e7a3665f808537b72d240+local-7b0a75063da5`。本批依次实施基地口粮配给、河畔运单密藏、河畔拾荒者撤退及工坊有偿加急。沿用原有三级士气，节约配给的上限为「稳定」。

**口粮配给已冻结**，程序 `2bb7d24b7550`，74/74 项专项通过。宿舍菜单可对照两档，确认落盘后跨日结算；自动供给范围保留，不动未经授权的个人库存。[专项](<E:/RaidlineBuild/verification/local-multistage-v3/ration-official.log>) · [八人基地预览](<E:/RaidlineBuild/verification/local-multistage-v3/ration-ui-images/ration-policy-1280-zh.png>)

**现场运单与本局密藏已冻结**，程序 `a4f8b9be9e51`，17/17 项专项通过。48个种子逐项核对战利品预算、资产身份与敌人；SDL 从普通河畔出击验证右键读文件、原迷雾逐格相同、偶然发现与现场搜取。[回归](<E:/RaidlineBuild/verification/local-multistage-v3/field-cache-official.log>) · [箱标](<E:/RaidlineBuild/verification/local-multistage-v3/field-cache-sdl-images/field-cache-map-1280-zh.png>)

原四小时结束后，用户要求完成剩余两项并交付验收。此轮继续使用同一分支、普通游玩入口与隔离试玩档。

**河畔拾荒者撤退已冻结**，程序 `8080799d3766`。28 项领域、移动和存档回归及 1 项 SDL 普通出击回归通过。36 个种子覆盖两名持枪者的合法退路。实测同一局追上一个、放走另一个，分别核对原枪剩余弹药、逃离后无尸可搜和地图视野。去掉旧局退路字段后，存档仍能逐字节往返。[专项回归](<E:/RaidlineBuild/verification/local-multistage-v3/retreat-release.log>) · [实战截图](<E:/RaidlineBuild/verification/local-multistage-v3/retreat-sdl-images/riverside-retreat-1280-zh.png>)

**工坊有偿加急已冻结**，程序 `a4a3c9b57a3f`。28 项领域、服务与存档回归及 2 项 SDL 交互通过。每工单可以支付一件选中的仓库电子元件，扣除当前剩余工作量的一半（向下取整）。仓库有空间时产物自动入库，满仓时保留原件待领。试玩档已验证按新期限交付原来的两枚诱饵。供电预估计入绑定在本单上的剩余燃油，收储和取消沿用原规则。[工坊专项](<E:/RaidlineBuild/verification/local-multistage-v3/expedite-release.log>) · [中文界面](<E:/RaidlineBuild/verification/local-multistage-v3/expedite-sdl-images/workshop-expedite-960-zh.png>) · [联合实走](<E:/RaidlineBuild/verification/local-multistage-v3/sixteen-package-play.log>)

## 联合交付 · 已通过人工验收

Windows Debug 全量通过 **3549/3549**，零跳过，用时 370.02 秒。原 14 项 3 ms 性能门槛通过。千件资产真实磁盘存档检查 P95 **4.6679 ms**（门槛 8 ms）。此前主线上 8.0073 ms 的记录仍属于原基线。试玩包包含 84 个按清单校验的运行文件，程序 SHA-256 为 `a4a3c9b57a3fc0b131cc3d9c94ff02f69c00b2caa3c5a277464536408d9b42cf`。对比前后 156 个文件，工业郊区候选、原固定入口及玩家存档的哈希均一致。用户已验收上述指纹的十六项玩法，授权将本候选提交、推送并创建 PR，待 Windows 和 Ubuntu CI 均通过后合并。[全量报告](<E:/RaidlineBuild/verification/local-multistage-v3/sixteen-stage-final.xml>) · [试玩指南](../../acceptance/local-multistage-v3.md)

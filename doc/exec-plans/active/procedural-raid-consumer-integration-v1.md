# Frontier Exchange 既有消费者整合 v1

## 产品结果与范围

本宏切片让既有 Raid 玩法真正消费 `Frontier Exchange` 的随机大地图，而不是只在运行时读取正确坐标却让玩家无法理解目标。玩家打开战术地图时能够看到已知的救援目标和主动选择的失物缓存；高危控制点在探索发现后显示。迁入 Ashworks 后发起的基地外围清剿实际进入 Frontier Exchange，每个 seed 继续使用同一套随机道路、资源点、遭遇、撤离和冻结结算合同。

基线为 `origin/main@f095022`，分支为 `codex/procedural-raid-consumer-integration-v1`。本轮继续使用代码图形和双语文字；不生成或接入新美术，不修改 Manifest。

明确排除：程序化室内、新地图主题、新 AI、实时守城、AI 小队、派系、传统任务面板、复杂穿透/断肢和正式内容生产。

## 既有事实与缺口

- 普通、信号和轻装撤离、高危控制/高级资源区、救援、失物缓存、压力出生和固定室内入口已由 layout v4 锚点冻结，GameSession 从冻结锚点构造运行时。
- 情报和探索迷雾已经控制撤离、资源点、敌情和固定室内投影；完整静态调试地图不授予实时敌情。
- 救援与自行寻回在 25600×14400 世界中只有近屏世界标记，战术地图没有任务投影；主动选择失物记录后仍可能无法定位缓存。
- 外围清剿领域与 Settlement 已完成，但两个基地地点仍只指向固定地图，随机地图没有实际清剿消费者。

## 接口与所有权

- 新增只读 `RaidTacticalObjective`：类型、冻结范围和可见性策略。救援与自行寻回属于 Briefed，进入行动时始终在战术地图可见；高危控制点属于 Explored，只有探索到附近或关闭调试迷雾时显示。
- `GameSession` 从 `PendingRaidSnapshot` 与 layout 锚点建立目标投影；App 只绘制标记，不读取或修改 Profile，不根据文字推断目标类型。
- 外围清剿仍由 `BasePerimeterSweepSnapshot` 唯一保存地点、减值和完成事实；战术地图只显示 ACTIVE/SECURED 状态，最终减值仍只在唯一 Settlement 提交。
- Ashworks 的外围清剿地图改为 `map.raid.frontier_exchange`。内容升级为 v49，schema 保持 v37；v48 与 v47 存档继续显式读取，既有 pending Raid 不重生成。

## 验证、交付与回滚

- 自动化覆盖 Briefed/Explored/FullStaticMap 可见性、F10 不泄露实时敌人、救援/失物/高危冻结位置一致、Ashworks 随机清剿 Deploy/提前撤离/完成结算/异常恢复，以及至少 128 个 seed 的关键目标可达与无非法重叠。
- 完成 Windows Debug 全目标、完整 CTest 和 exact-head Windows/Ubuntu CI 后，由用户正常游玩统一验收；开发代理不启动游戏。
- 回滚顺序为 App 投影、运行时/服务投影、Ashworks 内容与兼容、文档；普通 revert，不重写历史、不强推。

## 进度

- [x] PR #113 经用户验收并以 `f095022` 合入 main。
- [x] 审计既有消费者、冻结锚点、运行时和结算边界。
- [x] 实现统一战术目标投影和外围清剿随机地图消费者。
- [x] 完成 focused tests、Windows Debug 全目标与完整 CTest（292/292 定向、1283/1283 完整）。
- [ ] 提交、推送 Draft PR 并完成 exact-head 双平台 CI。
- [ ] 用户正常游玩验收。

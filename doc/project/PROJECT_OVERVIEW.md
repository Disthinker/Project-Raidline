# Project Raidline 项目概览

Project Raidline 是一款 2D 轻斜俯视角、写实可读像素风的灾后撤离生存游戏：

`Base 整备 -> Raid 搜索/战斗/规避 -> 管理空间与资源 -> 撤离或失败 -> 结算与长期积累 -> 再次出击`

四个产品支柱是高风险撤离、空间型格子背包、清晰可读的近距离战斗、短局冒险与长期积累。剧情采用环境碎片叙事，玩法规则必须直接清晰。

技术栈为 C++20、SDL3、SDL3_image、fmt、GoogleTest、CMake/Ninja 和 vcpkg。项目采用四库模块化单体；App 负责输入与只读表现，领域负责所有权、合法性、结算和持久化。

Core Extraction Alpha 与后续 Survival Loadout/Combat 基础切片已形成可反复游玩的接受基线。稳定能力见 `CURRENT_STATE.md`，未解决风险见 `KNOWN_ISSUES.md`，产品阶段见 `ROADMAP.md`。新任务先从 `doc/context/INDEX.md` 路由，不默认读取全部历史。

Week 1–28 和 completed ExecPlan 是历史证据。新开发按可运行、可测试、可回滚的产品切片交付。正式 Grab/Scratch/Bite 动画及未授权美术/音频生产保持暂停。

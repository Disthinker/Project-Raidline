# Project Raidline 项目概览

Project Raidline 是一款 2D 轻斜俯视角、写实可读像素风的灾后撤离生存游戏。核心循环是：

`Base 整备 → Raid 搜索/战斗/规避 → 管理空间与资源 → 撤离或失败 → 结算与长期积累 → 再次出击`

四个产品支柱：高风险撤离、空间型格子背包、清晰可读的近距离战斗、短局冒险与长期积累。剧情采用环境碎片叙事，玩法规则必须直接清晰。

当前目标是 Core Extraction Alpha。范围只由外部 GDD 的 `05_Core_Extraction_Alpha_首阶段功能规格.md` 决定；活动执行计划见 `doc/exec-plans/active/core-extraction-alpha.md`，产品路线见 `doc/project/ROADMAP.md`，实时状态见 `doc/project/CURRENT_STATE.md`。

技术栈为 C++20、SDL3、SDL3_image、fmt、GoogleTest、CMake/Ninja 和 vcpkg。业务逻辑尽量保持 SDL 无关，App 负责输入翻译与只读投影，领域负责所有权、合法性、结算和持久化。

Week 1–28 构成历史 V0；Week29 和库存修复仍在独立分支。新开发不再按 Week 组织，而按可运行、可测试、可回滚的垂直产品切片交付。

正式 Grab/Scratch/Bite 动画及所有新正式美术/音频生产保持暂停，除非用户重新授权命名包。

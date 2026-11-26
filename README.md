# 超级地球保卫战 (Super Earth Defense)

适用于 **HellForge** 的《绝地潜兵 2》独立模组：通过运行时本地 HTTP 响应重写，开放超级地球全部七座巨型城市与光能族入侵战役，并在联机时自动同步房主的模组开关设置。

> 当前版本：**0.4.4** · 适用环境：Windows / **HellForge 0.5.0-beta（`2436b97`）** / 原生 **SDK 20** / 游戏构建 **`25480438`**

---

## 难度

超级地球暂不支持 N11。启动游戏后，战况桌会过滤超级地球的 N11 行动，请选择 N10 或以下。其他星球的 N11 可照常选择。

## 敌军变种（超级地球全境）

在「超级地球保卫战」的变种模式中选原有战况、全部或自定义。保卫战开启时，三种模式都会移除超级地球上的霸王虫（虫巢领主）及其战况标记。「原版」保留其余原有变种，「全部」开放其余 11 种。旧版的自定义勾选会保留其余 11 项。「自定义」允许任意组合，包括关闭光能族入侵效果 1413。点击「应用」后在下一次战况刷新生效。房主决定整颗超级地球的变种池，每座城市仍可单独选择入侵种族。实际出敌取决于任务、难度和游戏的生成规则。

变种按虫族、机器人、光能族排列。七座城市和变种的简体中文名称来自游戏构建 25480438 的简体中文文本表。

## 逐城入侵设置

在 Mod Options Menu 的「超级地球保卫战」页，分别给七座城市选择虫族、机器人或光能族，然后点击「应用」。联机跟随房主，总开关和区域分配在下一次银河战争状态刷新时生效。默认区域 3（棒约克）为虫族、区域 6（仰齐浜）为机器人，其余五座为光能族。设置会写入本机同目录的 `SuperEarthDefense.cities`。

本版通过区域占领者与恢复速率表达虫族和机器人选择，光能族沿用原有的入侵战役和城市状态。任务卡与空降后实际生成的敌人需要游戏内验证。

**星图重叠修复。** 构建 25480438 的冻结战况将星球 264、265 和超级地球放在同一坐标。本版在星图数据中禁用匹配的两条同名记录，并在「所有星球开放」模式下过滤它们在原点的自动战役。导入后重新启动游戏，让星图重新读取星球定义。

## 主要特性

- **七座巨型城市开放：** 启用后在超级地球（`warId: 801`, `index: 0`）开放全部七座巨型城市、光能族战役 `51099` 与入侵效果 `1413`，其余星球沿用当前银河战争模式。
- **独立纯 Mod 架构：** 无需替换 HellForge 后端或手动执行安装脚本，直接通过原生 DLL Hook 本地 `libcurl.dll` 响应并保持原始 `Content-Length` 不变。
- **联机设置同步：** 基于 HellForge SDK 16 的 `coop: "shared"` 共享状态机制，房主每 250 ms 同步一次开关状态；队友在下次银河战争刷新时自动应用房主设置。
- **游戏内菜单支持：** 内置菜单补丁（`9ba626afa44a3aa3.patch_0`），配合 **Bingus Shared Loader v20+**、**HD2Runtime 0.50.1** 与 **Mod Options Menu** 可在 **设置 → 模组 → 超级地球保卫战** 中切换开关；未安装菜单依赖时默认开启（或沿用本地保存的状态）。

## 安装与使用

1. 下载发布包 `SuperEarthDefense-0.4.4-HellForge.zip`。
2. 在 **HellForge 启动器 → Mods → 添加模组** 中导入该 ZIP 并启用。
3. 使用平时的启动器开始游戏。**联机时房主和队友需启用同一版本的 Mod。**

### 前置依赖

- **必需环境：** HellForge **0.5.0-beta（`2436b97`）**、游戏构建 **`25480438`**
- **可选依赖（用于游戏内菜单开关）：**
  - Bingus Shared Loader **v20+**
  - HD2Runtime **0.50.1**
  - Mod Options Menu

### 联机与设置说明

- 本地开关配置文件路径：`%LOCALAPPDATA%\CowboyBingus\Helldivers2\SuperEarthDefense.enabled`（内容为 `1` 或 `0`，默认开启）。
- 联机时以**房主**的设置为准；队友在房内修改自己的菜单开关不会影响当前房间，只会在离房后或以后自己开房时生效。
- 诊断日志：`mod-pack.35c39371-a83a-5553-825d-7ceefb7c5ff2.log`。正常命中时会记录 `Super Earth Defense: applied to WarSeason Status`。
- 已知待确认项：两端星图表现、晚加入（Late Join）以及联机空降流程仍需进一步实机验证。

## 目录结构

```text
SuperEarthDefenseStandalone/
├── Build-Mod.ps1             # PowerShell 7.5 构建与打包脚本（生成 war_preset.h、编译 DLL 并打包 ZIP）
├── hellforge-mod.json        # HellForge Mod 清单（SDK 20、coop: shared、游戏构建 25480438）
├── README.md                 # 说明文档
├── menu/addon.lua            # 城市与变种设置菜单
└── src/
    ├── super_earth_defense.c # 原生 Mod 入口：libcurl Hook、开关读取与联机状态同步
    ├── n11_board.c / .h      # 超级地球 N11 行动筛选
    ├── war_json.h            # 轻量 JSON 重写器头文件
    └── war_json.c            # /WarSeason/801/Status 与 /WarSeason/801/warinfo 响应重写实现
```

## 从源码构建

构建脚本依赖 PowerShell 7.5+、HellForge 安装目录自带的 Zig 工具链与 SDK 20，同级工作区 `../SuperEarthDefenseTest` 中的预设生成脚本和菜单资源声明，以及 `../.tools/template/build.ps1` 中的 HD2Runtime 打包器：

```powershell
.\Build-Mod.ps1 -HellForgeInstall 'E:\Hellforge\HellForge'
```

构建完成后，安装包会输出至 `build/SuperEarthDefense-0.4.4-HellForge.zip`。

---

# Super Earth Defense (English)

A standalone **HellForge** native mod for **Helldivers 2** that opens all seven Mega Cities on Super Earth and the Illuminate invasion campaign by rewriting local war-season HTTP responses at runtime, with automatic host-to-joiner setting synchronization in co-op.

> Version: **0.4.4** · Target: Windows / **HellForge 0.5.0-beta (`2436b97`)** / Native **SDK 20** / Game Build **`25480438`**

## City faction menu

In Mod Options Menu, open Super Earth Defense, choose Terminids, Automatons or Illuminate for each of the seven regions, and press Apply. The host's settings take effect at the next war status refresh. Region 3 defaults to Terminids, region 6 to Automatons, and the other five to Illuminate. These settings control regional ownership and regeneration under the Illuminate campaign. Mission generation and actual spawns still require in-game testing.

## Difficulty

Super Earth currently supports difficulties 1–10. The mod filters Super Earth difficulty 11 operations on the war table. Difficulty 11 remains selectable on other planets.

## Enemy variants

The planet-wide menu offers original war settings (default), all 11 supported enemy variants, or a custom selection. While the defense is enabled, all three modes exclude the Hive Lord and its war-table flag on Super Earth. Existing custom selections keep their other 11 settings. Apply on the ship. The next war status response uses the host’s selection. Eligible variants still depend on the mission and difficulty. The menu groups the variants by faction. City and variant labels use the Simplified Chinese text from game build 25480438.

## Features

- **Seven Mega Cities Unlocked:** Opens all seven Mega Cities on Super Earth (`warId: 801`, `index: 0`), Illuminate campaign `51099`, and invasion effect `1413` while keeping other planets on the active Galactic War mode.
- **Standalone Native Architecture:** No backend replacement or setup script required. Hooks `libcurl.dll` locally and preserves the original `Content-Length` using trailing space padding.
- **Co-op Setting Sync:** Uses HellForge SDK 16 shared session state (`coop: "shared"`). The host publishes its toggle every 250 ms, and joiners apply it on the next Galactic War refresh.
- **In-Game Menu Integration:** Bundles the menu patch (`data/9ba626afa44a3aa3.patch_0`) for **Settings → Mods → 超级地球保卫战** when **Bingus Shared Loader v20+**, **HD2Runtime 0.50.1**, and **Mod Options Menu** are installed. Defaults to enabled if no menu dependencies are present.

## Installation

1. Download `SuperEarthDefense-0.4.4-HellForge.zip`.
2. Import and enable the ZIP in **HellForge → Mods → Add Mod**.
3. Launch the game as usual. **All players in a co-op session must run the same version of the mod.**

## Diagnostics

- Local preference file: `%LOCALAPPDATA%\CowboyBingus\Helldivers2\SuperEarthDefense.enabled` (`1` or `0`, defaults to enabled).
- Log file: `mod-pack.35c39371-a83a-5553-825d-7ceefb7c5ff2.log` (logs `Super Earth Defense: applied to WarSeason Status` on success).
- Note: Two-client galactic map display, late-join behavior, and co-op hellpod drop still require further live-play verification.

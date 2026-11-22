# 超级地球保卫战 (Super Earth Defense)

适用于 **HellForge** 的《绝地潜兵 2》独立模组：通过运行时本地 HTTP 响应重写，开放超级地球全部七座巨型城市与光能族入侵战役，并在联机时自动同步房主的模组开关设置。

> 当前版本：**0.4.0** · 适用环境：Windows / **HellForge 0.4.0-beta（`d8a8b46`）** / 原生 **SDK 16** / 游戏构建 **`25480438`**

---

## 主要特性

- **七座巨型城市开放：** 启用后在超级地球（`warId: 801`, `index: 0`）开放全部七座巨型城市、光能族战役 `51099` 与入侵效果 `1413`，其余星球沿用当前银河战争模式。
- **独立纯 Mod 架构：** 无需替换 HellForge 后端或手动执行安装脚本，直接通过原生 DLL Hook 本地 `libcurl.dll` 响应并保持原始 `Content-Length` 不变。
- **联机设置同步：** 基于 HellForge SDK 16 的 `coop: "shared"` 共享状态机制，房主每 250 ms 同步一次开关状态；队友在下次银河战争刷新时自动应用房主设置。
- **游戏内菜单支持：** 内置菜单补丁（`9ba626afa44a3aa3.patch_0`），配合 **Bingus Shared Loader v20+**、**HD2Runtime 0.50.1** 与 **Mod Options Menu** 可在 **设置 → 模组 → 超级地球保卫战** 中切换开关；未安装菜单依赖时默认开启（或沿用本地保存的状态）。

## 安装与使用

1. 下载发布包 `SuperEarthDefense-0.4.0-HellForge.zip`。
2. 在 **HellForge 启动器 → Mods → 添加模组** 中导入该 ZIP 并启用。
3. 使用平时的启动器开始游戏。**联机时房主和队友需启用同一版本的 Mod。**

### 前置依赖

- **必需环境：** HellForge **0.4.0-beta（`d8a8b46`）**、游戏构建 **`25480438`**
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
├── hellforge-mod.json        # HellForge Mod 清单（SDK 16、coop: shared、游戏构建 25480438）
├── README.md                 # 说明文档
└── src/
    ├── super_earth_defense.c # 原生 Mod 入口：libcurl Hook、开关读取与联机状态同步
    ├── war_json.h            # 轻量 JSON 重写器头文件
    └── war_json.c            # /WarSeason/801/Status 与 /WarSeason/801/warinfo 响应重写实现
```

## 从源码构建

构建脚本依赖 PowerShell 7.5+、HellForge 安装目录自带的 Zig 工具链与 SDK 16，以及同级工作区 `../SuperEarthDefenseTest` 中的预设生成脚本与菜单补丁包：

```powershell
.\Build-Mod.ps1 -HellForgeInstall 'E:\Hellforge\HellForge'
```

构建完成后，安装包会输出至 `build/SuperEarthDefense-0.4.0-HellForge.zip`。

---

# Super Earth Defense (English)

A standalone **HellForge** native mod for **Helldivers 2** that opens all seven Mega Cities on Super Earth and the Illuminate invasion campaign by rewriting local war-season HTTP responses at runtime, with automatic host-to-joiner setting synchronization in co-op.

> Version: **0.4.0** · Target: Windows / **HellForge 0.4.0-beta (`d8a8b46`)** / Native **SDK 16** / Game Build **`25480438`**

## Features

- **Seven Mega Cities Unlocked:** Opens all seven Mega Cities on Super Earth (`warId: 801`, `index: 0`), Illuminate campaign `51099`, and invasion effect `1413` while keeping other planets on the active Galactic War mode.
- **Standalone Native Architecture:** No backend replacement or setup script required. Hooks `libcurl.dll` locally and preserves the original `Content-Length` using trailing space padding.
- **Co-op Setting Sync:** Uses HellForge SDK 16 shared session state (`coop: "shared"`). The host publishes its toggle every 250 ms, and joiners apply it on the next Galactic War refresh.
- **In-Game Menu Integration:** Bundles the menu patch (`data/9ba626afa44a3aa3.patch_0`) for **Settings → Mods → 超级地球保卫战** when **Bingus Shared Loader v20+**, **HD2Runtime 0.50.1**, and **Mod Options Menu** are installed. Defaults to enabled if no menu dependencies are present.

## Installation

1. Download `SuperEarthDefense-0.4.0-HellForge.zip`.
2. Import and enable the ZIP in **HellForge → Mods → Add Mod**.
3. Launch the game as usual. **All players in a co-op session must run the same version of the mod.**

## Diagnostics

- Local preference file: `%LOCALAPPDATA%\CowboyBingus\Helldivers2\SuperEarthDefense.enabled` (`1` or `0`, defaults to enabled).
- Log file: `mod-pack.35c39371-a83a-5553-825d-7ceefb7c5ff2.log` (logs `Super Earth Defense: applied to WarSeason Status` on success).
- Note: Two-client galactic map display, late-join behavior, and co-op hellpod drop still require further live-play verification.

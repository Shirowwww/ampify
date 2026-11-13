---
title: 快速开始
description: 安装 Ampify、选择主题、回到原版 Apple Music，以及 Apple Music 更新后 Ampify 关闭时该怎么办。
---

# Ampify 快速开始

Ampify 为 Windows 版 Apple Music 增加主题、扩展和应用。它从不修改 Apple Music 自己的文件：卸载 Ampify，
就能拿回原版应用。详细文档为英文。

## 你需要什么

- Windows 10 或 11（x64），并且已从 Microsoft Store 安装 Apple Music 并登录。Ampify 从不询问你的 Apple ID。
- 经过测试的 Apple Music 版本（`ampify versions` 会列出）。换成其他版本时，Apple Music 照常打开，
  Ampify 保持关闭（见[安全模式与新的 Apple Music 版本](#safe-mode-and-new-apple-music-versions)）。

## 安装

先关闭 Apple Music，也要从时钟旁边的通知区域退出，它可能藏在那里。

**用 zip 安装。** 从 [Releases](https://github.com/Shirowwww/ampify/releases) 页面下载 `ampify-<version>-windows-x64.zip`，
解压整个 zip（右键，全部解压缩），然后双击新文件夹里的 `ampify_setup.exe`。如果在 zip 里面直接运行，安装程序会提示缺少文件。
如果 Windows 显示“Windows 已保护你的电脑”，点“更多信息”，再点“仍要运行”：这些文件目前还没有代码签名。
安装程序会先说明它要做什么，再逐步显示进度；如果 Apple Music 开着，它会先把它关掉。遇到还没测试过的 Apple Music 版本，
它会告知这一点，然后照样安装：之后在该版本测试完成之前，Ampify 在 Apple Music 里保持关闭
（见[安全模式与新的 Apple Music 版本](#safe-mode-and-new-apple-music-versions)）。

**用 PowerShell 安装。** 打开它（开始菜单，输入“PowerShell”），然后粘贴：

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
```

这一行会下载最新版本，校验它的 SHA-256，然后运行安装程序。想在运行前先看看脚本，可以把它保存下来，打开阅读，
再自己运行：

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 -OutFile install.ps1
notepad install.ps1
powershell -ExecutionPolicy Bypass -File install.ps1
```

`-ExecutionPolicy Bypass` 只让这一个脚本得以运行，不会更改 Windows 的设置。

安装程序会把 Ampify 放进 `%LOCALAPPDATA%\Programs\Ampify`，把你的主题和设置放进 `%USERPROFILE%\Ampify`，并添加：

- 一个启动项（任务管理器 > 启动应用），每次登录时运行启动器。它让 Windows 随 Apple Music 一起启动启动器，
  所以 Ampify 在 Apple Music 打开时加载，检查一次更新，然后结束：Ampify 不会在后台一直运行。如果 Windows 拒绝这项设置，
  启动器会保持运行，等待 Apple Music；
- 把 `ampify` 加入你的 Path，任何新开的终端都能用；
- 在“设置 > 应用 > 已安装的应用”中出现“Ampify for Apple Music”。

## 打开 Apple Music

像平常一样从它自己的图标打开 Apple Music。装上 Ampify 后，你会看到：

- 侧边栏底部多了一项 **Ampify**，点它打开 Marketplace；
- Apple Music 设置的“高级”下多了一行 **Ampify**，点它打开 Ampify 的设置；
- 播放栏右侧的 **Now Playing** 按钮，点它打开带歌词的 Apple Music 全屏播放器（按 Esc 关闭）。

## 选一个主题

刚装好时，Apple Music 看起来和以前一样：Ampify 不自带主题，第一个主题要从 Marketplace 获取。

1. 点击侧边栏底部的 **Ampify**。Marketplace 会打开。
2. 在**主题**下，点你想要的那个的**安装**，例如 Glass 或 Artwork：它会立刻生效。
3. 如果主题有多种配色方案，在菜单里选一个，例如 Dark 或 Light。

改变会立刻显示。**关闭**（在正在使用的主题上）和**使用 Apple Music 默认外观**（在主题列表底部）都会回到原来的外观，
**卸载**则会删除你不用的主题。Marketplace 读取的是[公开目录](https://github.com/Shirowwww/ampify-catalog)，
所以需要联网；已经安装的主题没有网络也能继续使用。

这里用到的词：**主题**改变 Apple Music 的外观，**扩展**增加功能，**应用**是侧边栏里有入口的独立页面，
**代码片段**是在主题之外另行开启的几行样式。**开发者**标签页里是给制作扩展的人看的示例。
**配色方案**指主题可能提供的浅色或深色版本。

播放栏的位置对所有主题是同一个选择，在“设置 > Ampify > 外观 > 播放栏”，位于主题下方。
“跟随主题”保留每个主题自己的位置；“顶部”“底部”和“底部，进度条在按钮下方”（进度线从按钮下方穿过，
夹在已播放时间和歌曲时长之间）对所有主题都有效。

## 扩展

打开 Apple Music 的设置并点 **Ampify**，或者在 Marketplace 里点**设置**。Ampify 的设置页有五个标签页：**外观**
（主题、强调色、玻璃效果，以及当前主题和代码片段的设置）、**窗口**、**播放器**、**声音和扩展**（每个扩展一个开关）
和**高级**（网络、稳定性、更新和帮助）。主题、代码片段或扩展的设置，在你展开之前一直折叠在它的名称下面。
标签页上方的搜索框会同时搜索所有标签页，折叠起来的设置也包括在内。更改会立即生效。

你安装的扩展一开始都是关闭的，打开之前，你会先看到它需要的权限。Ampify 自带五个：Now Playing、歌曲面板
（一栏关于正在播放歌曲的信息，用播放栏上的按钮打开）、悬停显示时间（在进度线上显示指针所在位置的歌曲时间，默认开启）、
播放守护（重启被 Apple Music 卡住的歌曲，默认开启），以及**声音**下的全景声均衡器：杜比全景声歌曲往往比立体声歌曲小声，
均衡器会把它们调到同样的响度，这样歌曲之间就不必去调音量。你打开它之前，它是关闭的。

## 语言

Ampify 支持英语、法语、德语、西班牙语、葡萄牙语（巴西）、俄语、日语、简体中文、意大利语和韩语。
它跟随 Windows 的显示语言（设置 > 时间和语言 > 语言和区域）。繁体中文环境显示简体文本，尚未翻译的文本显示为英文。
要固定使用某种语言：

```
ampify config language de
```

可以填 `en`、`fr`、`de`、`es`、`pt`、`ru`、`ja`、`zh`、`it` 或 `ko`，然后重启 Apple Music。
`ampify config language ""` 会重新跟随 Windows。

## 回到 Apple Music

三种方式，从最轻的开始：

1. **只关主题。** 在 Marketplace 里，点正在使用的主题上的**关闭**，或点**使用 Apple Music 默认外观**。
2. **暂时关闭 Ampify**，什么都不删除。目前还没有按钮：在终端运行 `ampify restore`，
   想再打开就运行 `ampify apply`。
3. **卸载。** `ampify uninstall`，或者“设置 > 应用 > 已安装的应用 > Ampify for Apple Music”（见[卸载](#uninstall)）。

```
ampify restore     # Apple Music starts without Ampify from now on
ampify apply       # Ampify loads with Apple Music again
```

已经打开的 Apple Music 会一直带着 Ampify，直到你把它关掉。你的主题、扩展和设置都会保留。

## 安全模式与新的 Apple Music 版本 {#safe-mode-and-new-apple-music-versions}

- 如果 Apple Music 在启动时连续两次崩溃或卡死，Ampify 会在下一次启动时自行关闭。等 Apple Music 在窗口正常响应的状态下
  运行满 8 秒，Ampify 会在再下一次启动时重新尝试。
- Ampify 只会在测试过的 Apple Music 版本上加载。Apple Music 通过 Microsoft Store 自动更新，所以更新之后，
  在新版本测试完成之前，Ampify 可能保持关闭。这时 Apple Music 照常打开，只是没有你的主题，什么也没坏。

无论哪种情况，第一次发生时 Windows 都会显示一条通知（“Ampify 暂时关闭”或“Ampify 跳过了这次启动”），
每个新的 Apple Music 版本一次。如果通知说 Ampify 拿到了新的已测试版本列表，请完全关闭 Apple Music
（通知区域里的也要关），再重新打开。否则，等新版本测试完成，Ampify 就会回来：新的 Ampify 版本会带来它
（更新会自动安装），而 `ampify versions update` 会立刻去找这份列表。`ampify doctor` 随时显示同样的状态，
`ampify notice safe-mode --dry-run` 会打印通知的内容。

## 更新

Ampify 会自动更新。每天一次，在 Apple Music 关闭时，它会查找新版本，校验 SHA-256，安装并显示一条通知；
新版本在下次启动 Apple Music 时加载。如果你让 Apple Music 一直开在通知区域，更新会等到你退出它或重启电脑。
在 Apple Music 的**设置 > Ampify > 高级 > Ampify 更新**里，可以选择先询问，或者从不更新
（`ampify config kit_update ask` 或 `off`）。“先问我”从不会弹出需要你等待的窗口：通知会提供新版本，
点一下就安装（立即安装，或在 Apple Music 关闭后马上安装），而无人理会的通知一周后才会再出现。

想手动更新，可以再运行一遍安装命令，或者在 Apple Music 关闭时运行 `ampify update --apply`。
各种方式都会保留你的设置。

你的主题、代码片段和扩展也会跟着更新。新版本第一次启动时，Ampify 会把目录里的每个项目升级到新版本，
并把已停用的代码片段换成接替它的那个，设置也一并带过去。没有网络时它们保持原样，下次启动再试。
`ampify update --elements` 可以手动完成这件事。

## 卸载 {#uninstall}

在终端运行 `ampify uninstall`，或者打开“设置 > 应用 > 已安装的应用”，找到“Ampify for Apple Music”，
点三个点，再点卸载。两种方式打开的是同一个卸载程序：它会移除程序、随 Windows 启动的设置、在 Apple Music 中的加载，
以及“设置 > 应用”里的条目，如果 Apple Music 开着，会先把它关掉。一个默认不勾选的复选框，可以让你一并删除
`%USERPROFILE%\Ampify` 中的主题、扩展和设置。之后 Apple Music 会像以前一样打开和播放。
`ampify uninstall --yes` 不弹窗口直接卸载，并保留你的文件（加上 `--delete-data` 则一并删除）。

## 出了问题

1. 运行 `ampify doctor`：它会检查 Ampify，并列出每个问题。
2. 运行 `ampify report`：它会在 `%USERPROFILE%\Ampify\reports` 生成一个匿名化的 zip，包含状态和日志
   （Ampify 设置页里的**导出报告**按钮做的是同一件事）。先看一遍内容，再把它附到
   [issue](https://github.com/Shirowwww/ampify/issues) 上。

日志在 `%USERPROFILE%\Ampify\logs\`。如果杀毒软件拦截了某个文件，记下威胁名称，在弄清它是什么之前，
什么都不要放行。

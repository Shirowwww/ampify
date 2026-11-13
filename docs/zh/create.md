---
title: 创作
description: 为 Ampify 制作主题、代码片段或扩展，在 Apple Music 中实时查看，并分享到目录。
---

# 为 Ampify 创作

主题就是一个装着几个文本文件的文件夹。扩展是一个 JavaScript 文件（也可以是 TypeScript，会替你编译）加一份清单。
你需要装好 Ampify 和 Apple Music，并在工作文件夹里打开一个终端。不用编译，也不用注册账号。详细文档为英文。

1. **从能运行的东西开始。**
   `ampify new theme my-theme`（也可以是 `extension`、`snippet`、`app`）会在当前文件夹生成一份完整的起步模板。
   它本来就能运行：你是在能用的东西上修改，而不是面对一张白纸。

2. **实时查看。**
   `ampify dev my-theme` 会把它复制进 Ampify、启用，并在你每次保存时重新加载。Apple Music 一秒内就会变化，
   不用重启。让终端一直开着。

3. **找到要设计的地方。**
   主题使用 Apple Music 各个区域的固定名称：`.sidebar`、`.playbar`、`.lcd`、`.track-row`、`.card`，还有一百多个
   （[完整列表](../themes/names.md)）。其他地方，先运行 `ampify devtools`，再用 `ampify inspect`，
   就能看到实时的界面树和每个元素的选择器。

4. **塑造它。**
   颜色写在 `colors.ini` 里，每种配色方案一个小节。形状、间距、悬停效果、过渡和动画，写在 `user.css` 里，
   用你熟悉的 CSS。主题可以声明自己的设置（滑块、开关、选项），用户在 Apple Music 的设置中修改。
   参见[主题格式](../themes/format.md)。

5. **或者写行为。**
   扩展使用 `Ampify` API：播放器、歌曲、通知、菜单、面板、存储，以及清单中声明的网络请求。
   每项权限都会在安装前展示给用户。参见[扩展指南](../extensions/guide.md)和 [API](../extensions/api.md)。

6. **检查。**
   `ampify validate my-theme` 按与目录相同的规则检查清单和文件。

7. **分享。**
   `ampify publish my-theme` 会再检查一遍，然后替你在[公开目录](https://github.com/Shirowwww/ampify-catalog)
   上发起 pull request，不用克隆任何东西。合并之后，它会出现在每个 Ampify 的 Marketplace 和
   [图库](themes.html)里。包括预览在内的完整流程，只要
   [五步](../extensions/catalog.md#propose-an-element-in-five-steps)。
{: .steps}

## 一件事，一种做法

主题、代码片段和扩展共用同一套工具。两种项目都能做同一件事时，做法也相同，所以学会一种，其他的也就会了。

| 想要…… | 使用 | 适用于 |
|---|---|---|
| 提供设置 | 清单的 `settings` 字段（[清单](../extensions/manifest.md#settings)）：CSS 用 `@setting` 和 `--setting-<key>` 读取，JavaScript 用 `Ampify.config.get(key)` | 主题、代码片段、扩展、应用 |
| 翻译名称、描述和设置 | 清单的 `translations` 字段 | 所有项目 |
| 知道播放栏在哪里 | `@setting bar-position`，由用户为所有主题设定，或由主题的 `"playbar"` 决定 | 主题、代码片段 |
| 跟随面板的圆角 | `--panel-radius`（播放栏和显示屏另有 `--bar-radius`、`--lcd-radius`） | 由主题和代码片段设定；代码片段和扩展的面板会跟随 |
| 使用正在播放的封面的颜色 | CSS 中用 `--art-1` 到 `--art-3` 及其同类，JavaScript 中用 `Ampify.color.playing()` | 全部 |
| 画出随音乐跳动的条形 | `content: bars(n, shape, height)` | 主题、代码片段 |
| 给元素加模糊、改色或玻璃效果 | `filter` 和 `backdrop` | 主题、代码片段 |
| 按状态设计一行、一个开关或一个控件 | `:hover`、`:active`、`:focus`、`:current`、`:playing`、`:checked`、`:selected`、`:disabled` | 主题、代码片段 |
| 标出进度线上的一段 | `Ampify.ui.playbar.mark(id, from, to)` | 扩展 |
| 搭建界面 | `Ampify.ui` 的节点，或 `Ampify.React` 搭配 `Ampify.components`（按钮、开关、复选框、滑块、输入框、菜单、标签页、进度条、加载指示、分隔线等） | 扩展、应用 |

[主题格式](../themes/format.md#what-a-theme-tells-snippets-and-extensions)列出了主题会传给其他项目的内容，
[API](../extensions/api.md#55-interface-stable) 列出了每个节点和组件。

## 从 Spicetify 过来

Spicetify 的 `color.ini` 可以直接使用：放进主题文件夹，它的颜色就会装扮 Apple Music。`user.css` 则需要把
Spotify 的类名换成 Ampify 的名称。[哪些能沿用，哪些不能](../themes/spicetify.md)。

## 许可证

Ampify 采用 PolyForm Noncommercial License 1.0.0：除商业用途外可免费使用，并需保留署名。这份许可证不涉及你创作的内容：
只使用 Ampify 已公开的格式和 API 的主题、代码片段、扩展和应用都归你所有，可以选择任何许可证，商业与否都行。
`ampify new` 生成的起步模板和示例采用 MIT 许可证，可以放心复制。

## 继续深入

- [五分钟做一个主题](../themes/guide.md)
- [主题格式：每个属性和选择器](../themes/format.md)
- [Apple Music 界面的固定名称](../themes/names.md)
- [编写扩展](../extensions/guide.md)和[它的清单](../extensions/manifest.md)
- [命令行](../cli.md)

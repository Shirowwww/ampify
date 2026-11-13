---
title: 作る
description: Ampify 用のテーマ、スニペット、拡張機能を作り、Apple Music でそのまま確かめ、カタログで共有します。
---

# Ampify のものを作る

テーマは、いくつかのテキストファイルが入ったフォルダーです。拡張機能は、JavaScript ファイル (TypeScript でもかまいません。
こちらでコンパイルします) 1 つと、マニフェストです。必要なのは、Ampify と Apple Music、そして作業フォルダーで開いた
ターミナルだけ。コンパイルは不要で、アカウントも作りません。詳しいドキュメントは英語です。

1. **動くものから始める。**
   `ampify new theme my-theme` (`extension`、`snippet`、`app` も可) で、完成した出発点が今のフォルダーに作られます。
   最初から動くので、白紙ではなく、動くものを少しずつ変えていけます。

2. **その場で見る。**
   `ampify dev my-theme` は、作ったものを Ampify にコピーしてオンにし、保存のたびに読み込み直します。Apple Music は
   再起動なしで、1 秒以内に変わります。ターミナルは開いたままにしておきます。

3. **変える場所を見つける。**
   テーマは、Apple Music の各領域につけた固定の名前を使います。`.sidebar`、`.playbar`、`.lcd`、`.track-row`、
   `.card` など、100 以上あります ([全一覧](../themes/names.md))。それ以外は、`ampify devtools` のあとに
   `ampify inspect` を実行すると、動作中の画面の構造と、各要素のセレクターが見られます。

4. **形にする。**
   色は `colors.ini` に、カラースキームごとにセクションを分けて書きます。形、余白、ホバー時の効果、トランジション、
   アニメーションは、おなじみの CSS で `user.css` に書きます。テーマには独自の設定 (スライダー、スイッチ、選択肢) を
   持たせることができ、使う人は Apple Music の設定で変えられます。[テーマの形式](../themes/format.md)を参照してください。

5. **動作を書く。**
   拡張機能は `Ampify` API を使います。プレーヤー、曲、通知、メニュー、パネル、ストレージ、そしてマニフェストで宣言した
   Web への通信を扱えます。各権限は、インストール前に使う人へ表示されます。
   [拡張機能ガイド](../extensions/guide.md)と [API](../extensions/api.md) を参照してください。

6. **確かめる。**
   `ampify validate my-theme` は、カタログと同じ規則でマニフェストとファイルを検査します。

7. **共有する。**
   `ampify publish my-theme` は、もう一度検査してから、何もクローンせずに
   [公開カタログ](https://github.com/Shirowwww/ampify-catalog)へのプルリクエストを代わりに開きます。マージされると、
   すべての Ampify の Marketplace と[ギャラリー](themes.html)に並びます。プレビューを含む全手順は
   [5 つのステップ](../extensions/catalog.md#propose-an-element-in-five-steps)にまとめてあります。
{: .steps}

## 同じことは同じ方法で

テーマ、スニペット、拡張機能は、道具を共有しています。2 種類の項目が同じことをできるときは、同じやり方になっているので、
1 つで覚えたことはほかでも使えます。

| やりたいこと | 使うもの | 使える場所 |
|---|---|---|
| 設定を用意する | マニフェストの `settings` フィールド ([マニフェスト](../extensions/manifest.md#settings))。CSS は `@setting` と `--setting-<key>` で、JavaScript は `Ampify.config.get(key)` で読みます | テーマ、スニペット、拡張機能、アプリ |
| 名前、説明、設定を翻訳する | マニフェストの `translations` フィールド | すべての項目 |
| 再生バーの位置を知る | `@setting bar-position`。すべてのテーマ共通で使う人が決めるか、テーマの `"playbar"` が決めます | テーマ、スニペット |
| パネルの角の丸みに合わせる | `--panel-radius` (バーとディスプレイには `--bar-radius`、`--lcd-radius`) | テーマとスニペットが設定し、スニペットと拡張機能のパネルが従います |
| 再生中のジャケットの色を使う | CSS では `--art-1` から `--art-3` とその仲間、JavaScript では `Ampify.color.playing()` | すべて |
| 音楽に合わせて動くバーを描く | `content: bars(n, shape, height)` | テーマ、スニペット |
| 要素をぼかす、色を変える、ガラス効果をつける | `filter` と `backdrop` | テーマ、スニペット |
| 行、スイッチ、コントロールを状態に応じて装飾する | `:hover`、`:active`、`:focus`、`:current`、`:playing`、`:checked`、`:selected`、`:disabled` | テーマ、スニペット |
| 進行ラインの一部に印をつける | `Ampify.ui.playbar.mark(id, from, to)` | 拡張機能 |
| 画面を作る | `Ampify.ui` のノード、または `Ampify.React` と `Ampify.components` (ボタン、スイッチ、チェックボックス、スライダー、入力欄、メニュー、タブ、進行バー、読み込み表示、区切り線など) | 拡張機能、アプリ |

[テーマの形式](../themes/format.md#what-a-theme-tells-snippets-and-extensions)には、テーマがほかの項目に伝えるものが
載っています。[API](../extensions/api.md#55-interface-stable)には、すべてのノードとコンポーネントが載っています。

## Spicetify から来た方へ

Spicetify の `color.ini` は、そのまま使えます。テーマのフォルダーに置けば、その色が Apple Music に反映されます。
`user.css` は、Spotify のクラスではなく Ampify の名前に書き換える必要があります。
[引き継げるものと、引き継げないもの](../themes/spicetify.md)を参照してください。

## ライセンス

Ampify は PolyForm Noncommercial License 1.0.0 のもとにあります。商用を除くあらゆる用途で無料で、クレジットの表示は
残してください。このライセンスは、あなたが作るものには及びません。Ampify の公開されている形式と API だけを使う
テーマ、スニペット、拡張機能、アプリはあなたのものです。商用でもそうでなくても、好きなライセンスで公開できます。
`ampify new` が作る出発点とサンプルは MIT ライセンスなので、自由にコピーしてください。

## さらに進むには

- [5 分でテーマを作る](../themes/guide.md)
- [テーマの形式: すべてのプロパティとセレクター](../themes/format.md)
- [Apple Music の画面の固定名](../themes/names.md)
- [拡張機能を書く](../extensions/guide.md)と[そのマニフェスト](../extensions/manifest.md)
- [コマンドライン](../cli.md)

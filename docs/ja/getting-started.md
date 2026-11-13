---
title: はじめに
description: Ampify のインストール、テーマの選び方、元の Apple Music に戻す方法、そして Apple Music の更新後に Ampify がオフになったときの対処。
---

# Ampify をはじめる

Ampify は、Windows 版 Apple Music にテーマ、拡張機能、アプリを加えます。Apple Music 自身のファイルは一切変更しないので、
Ampify をアンインストールすれば、元のアプリがそのまま戻ります。詳しいドキュメントは英語です。

## 必要なもの

- Windows 10 または 11 (x64)。Microsoft Store から Apple Music をインストールし、サインインしておきます。Ampify が
  Apple ID を尋ねることはありません。
- テスト済みの Apple Music のバージョン (`ampify versions` で一覧が見られます)。それ以外のバージョンでは、Apple Music は
  いつもどおり開き、Ampify はオフのままです ([セーフモードと新しい Apple Music のバージョン](#safe-mode-and-new-apple-music-versions)を参照)。

## インストール

先に Apple Music を閉じてください。時計の横の通知領域に隠れていることがあるので、そこからも終了します。

**zip から。** [Releases](https://github.com/Shirowwww/ampify/releases) ページから `ampify-<version>-windows-x64.zip` を
ダウンロードし、zip をすべて展開して (右クリック、すべて展開)、できたフォルダーの `ampify_setup.exe` をダブルクリックします。
zip の中から直接実行すると、インストーラーはファイルが足りないと知らせます。Windows に「WindowsによってPCが保護されました」と
表示されたら、「詳細情報」、続いて「実行」をクリックしてください。ファイルにはまだコード署名がありません。インストーラーは、
これから行うことを先に表示し、実行中は各ステップを順に表示します。Apple Music が開いていれば、先に閉じます。まだテストされて
いないバージョンの Apple Music では、その旨を知らせたうえでインストールを続けます。その場合、そのバージョンがテストされる
まで、Apple Music の中で Ampify はオフのままです
([セーフモードと新しい Apple Music のバージョン](#safe-mode-and-new-apple-music-versions)を参照)。

**PowerShell から。** PowerShell を開き (スタートメニューで「PowerShell」と入力)、次を貼り付けます。

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
```

この 1 行は、最新のリリースをダウンロードし、SHA-256 を確認して、インストーラーを実行します。実行前にスクリプトを読みたいときは、
保存して開き、自分で実行します。

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 -OutFile install.ps1
notepad install.ps1
powershell -ExecutionPolicy Bypass -File install.ps1
```

`-ExecutionPolicy Bypass` は、Windows の設定を変えずに、このスクリプトだけを実行できるようにします。

インストーラーは、Ampify を `%LOCALAPPDATA%\Programs\Ampify` に、テーマと設定を `%USERPROFILE%\Ampify` に置き、
次のものも追加します。

- サインインのたびにランチャーを起動するスタートアップ項目 (タスク マネージャー > スタートアップ アプリ)。ランチャーを
  Apple Music と一緒に起動するよう Windows に伝えるので、Apple Music が開くときに Ampify が読み込まれ、アップデートを
  1 回確認して、終了します。Ampify がバックグラウンドで動き続けることはありません。Windows がこの設定を受け付けない
  環境では、ランチャーが動いたまま Apple Music を待ちます。
- 新しいターミナルで使えるよう、Path への `ampify`。
- [設定] > [アプリ] > [インストールされているアプリ] の「Ampify for Apple Music」。

## Apple Music を開く

いつものアイコンから Apple Music を開きます。Ampify が入ると、次のものが見えます。

- サイドバーの一番下の **Ampify** 項目。Marketplace が開きます。
- Apple Music の設定の「詳細」にある **Ampify** の行。Ampify の設定が開きます。
- 再生バーの右にある **Now Playing** ボタン。歌詞つきの Apple Music のフルスクリーンプレーヤーが開きます (Esc で閉じます)。

## テーマを選ぶ

インストール直後の Apple Music は、以前と同じ見た目です。Ampify にはテーマが入っていないので、最初の 1 つは Marketplace から選びます。

1. サイドバーの一番下の **Ampify** をクリックします。Marketplace が開きます。
2. **テーマ**の中から、使いたいものの **インストール** をクリックします。たとえば Glass や Artwork です。すぐに使われます。
3. テーマに複数のカラースキームがあれば、メニューから選びます。たとえば Dark や Light です。

変更はすぐに表示されます。**オフにする** (使用中のテーマ) と **Apple Music 標準の外観を使う** (テーマ一覧の一番下) は、
どちらも元の見た目に戻します。**アンインストール**は、使っていないテーマを削除します。Marketplace は
[公開カタログ](https://github.com/Shirowwww/ampify-catalog)を読むので、インターネット接続が必要です。インストール済みの
テーマは、接続がなくても使えます。

用語を整理します。**テーマ**は Apple Music の見た目を変えます。**拡張機能**は機能を加えます。**アプリ**はサイドバーに項目を持つ
独立したページです。**スニペット**は、テーマとは別にオンにする数行のスタイルです。**開発者**タブには、拡張機能を作る人向けの
サンプルがあります。**スキーム**は、テーマが用意することのある、明るい版または暗い版です。

再生バーの位置は、すべてのテーマ共通の 1 つの設定です。[設定] > [Ampify] > [外観] > [再生バー] の、テーマの下にあります。
「テーマどおり」は、各テーマ自身の位置のままにします。「上」「下」「下、ボタンの下に進行バー」(進行ラインがボタンの下、
経過時間と曲の長さの間を通ります) は、どのテーマでも有効です。

## 拡張機能

Apple Music の設定を開いて **Ampify** をクリックするか、Marketplace の **設定** をクリックします。Ampify の設定ページには
5 つのタブがあります。**外観** (テーマ、アクセントカラー、ガラス効果、使用中のテーマとスニペットの設定)、**ウインドウ**、
**プレーヤー**、**サウンドと拡張機能** (拡張機能ごとに 1 つのスイッチ)、**詳細** (ネットワーク、安定性、アップデート、ヘルプ)
です。テーマ、スニペット、拡張機能の設定は、開くまでその名前の下にたたまれています。タブの上の検索ボックスは、たたまれた
設定も含め、すべてのタブを一度に探します。変更はすぐに反映されます。

インストールした拡張機能は、最初はオフです。オンにする前に、その拡張機能が求める権限が表示されます。Ampify には 5 つが
付属します。Now Playing、曲パネル (再生中の曲についての列で、再生バーのボタンから開きます)、ホバー時の時間
(進行ラインの上で、ポインターの位置の曲の時間を表示。最初からオン)、再生ガード (Apple Music が止まったままにした曲を
再スタート。最初からオン)、そして **サウンド** にある Atmos レベラーです。Dolby Atmos の曲はステレオの曲より小さく聞こえることが
よくあります。レベラーは同じ音量にそろえるので、曲の合間に音量を触らずに済みます。オンにするまでは、オフです。

## 言語

Ampify は、英語、フランス語、ドイツ語、スペイン語、ポルトガル語 (ブラジル)、ロシア語、日本語、簡体字中国語、イタリア語、
韓国語に対応しています。Windows の表示言語 ([設定] > [時刻と言語] > [言語と地域]) に従います。繁体字中国語の環境では
簡体字のテキストを表示し、まだ翻訳されていないテキストは英語で表示します。言語を固定するには、次のようにします。

```
ampify config language de
```

`en`、`fr`、`de`、`es`、`pt`、`ru`、`ja`、`zh`、`it`、`ko` のいずれかを指定して、Apple Music を再起動します。
`ampify config language ""` で、また Windows に従います。

## Apple Music に戻す

軽いものから順に、3 つの方法があります。

1. **テーマだけ。** Marketplace で、使用中のテーマの **オフにする**、または **Apple Music 標準の外観を使う** をクリックします。
2. **しばらく Ampify をオフにする。** 何も削除されません。ボタンはまだないので、ターミナルで `ampify restore` を実行し、
   戻すときは `ampify apply` を実行します。
3. **アンインストール。** `ampify uninstall`、または [設定] > [アプリ] > [インストールされているアプリ] >
   「Ampify for Apple Music」です ([アンインストール](#uninstall)を参照)。

```
ampify restore     # Apple Music starts without Ampify from now on
ampify apply       # Ampify loads with Apple Music again
```

すでに開いている Apple Music は、閉じるまで Ampify が入ったままです。テーマ、拡張機能、設定は残ります。

## セーフモードと新しい Apple Music のバージョン {#safe-mode-and-new-apple-music-versions}

- Apple Music が起動時に 2 回続けてクラッシュまたはフリーズすると、Ampify は次の起動のために自分をオフにします。Apple Music が
  ウインドウの応答がある状態で 8 秒動いたら、その次の起動で Ampify はもう一度試します。
- Ampify は、テスト済みの Apple Music のバージョンでだけ読み込まれます。Apple Music は Microsoft Store を通じて自動で更新される
  ので、更新のあと、新しいバージョンがテストされるまで Ampify はオフのままのことがあります。そのとき Apple Music は
  テーマなしでいつもどおり開き、何も壊れていません。

どちらの場合も、最初にそうなったとき、Windows が通知を 1 つ表示します (「Ampify は一時的にオフです」または「Ampify は今回の
起動をスキップしました」)。Apple Music の新しいバージョンごとに 1 回です。Ampify がテスト済みバージョンの新しい一覧を受け取った
と表示されたら、Apple Music を通知領域も含めて完全に閉じ、もう一度開きます。そうでなければ、新しいバージョンのテストが
済んだときに Ampify が戻ります。新しい Ampify のリリースがそれを運んできます (アップデートは自動でインストールされます)。
`ampify versions update` は、一覧を今すぐ探します。`ampify doctor` はいつでも同じ状態を表示し、
`ampify notice safe-mode --dry-run` は通知の文面を出力します。

## アップデート

Ampify は自動で更新されます。1 日 1 回、Apple Music が閉じている間に、新しいリリースを探し、SHA-256 を確認し、
インストールして、通知を表示します。読み込まれるのは次に Apple Music を起動したときです。Apple Music を通知領域に開いたままに
している場合、アップデートは、終了するか PC を再起動するまで待ちます。Apple Music の **[設定] > [Ampify] > [詳細] >
Ampify のアップデート** で、先に確認する、または更新しない、を選べます (`ampify config kit_update ask` または `off`)。
「先に確認する」は、待たせるウインドウを開くことはありません。通知が新しいバージョンを知らせ、クリックするとインストールします
(すぐ、または Apple Music が閉じられ次第)。放置した通知は、1 週間後にまた出ます。

手動で更新するには、インストールの 1 行をもう一度実行するか、Apple Music を閉じた状態で `ampify update --apply` を実行します。
どの方法でも、設定は残ります。

テーマ、スニペット、拡張機能もあとに続きます。新しいリリースの最初の起動で、Ampify は各項目をカタログの新しいバージョンに
移し、廃止されたスニペットは、その役目を引き継いだスニペットに、設定ごと置き換えます。インターネットがなければそのままで、
次の起動でもう一度試します。`ampify update --elements` で手動でも行えます。

## アンインストール {#uninstall}

ターミナルで `ampify uninstall` を実行するか、[設定] > [アプリ] > [インストールされているアプリ] を開いて
「Ampify for Apple Music」を探し、3 つの点をクリックして、アンインストールを選びます。どちらも同じアンインストーラーを
開き、プログラム、Windows と一緒の起動、Apple Music への読み込み、[設定] > [アプリ] の項目を削除します。Apple Music が
開いていれば、先に閉じます。最初はオフになっているチェックボックスで、`%USERPROFILE%\Ampify` にあるテーマ、拡張機能、設定も
削除するか選べます。そのあとの Apple Music は、以前と同じように開き、再生します。`ampify uninstall --yes` は、
ウインドウなしで Ampify を削除し、ファイルは残します (`--delete-data` を付けると削除します)。

## うまくいかないとき

1. `ampify doctor` を実行します。Ampify を点検して、問題をすべて一覧にします。
2. `ampify report` を実行します。個人情報を除いた状態とログの zip を `%USERPROFILE%\Ampify\reports` に作ります
   (Ampify の設定ページの **レポートを書き出す** ボタンでも同じことができます)。内容を確認してから、
   [issue](https://github.com/Shirowwww/ampify/issues) に添付してください。

ログは `%USERPROFILE%\Ampify\logs\` にあります。ウイルス対策ソフトがファイルをブロックしたら、脅威の名前を控えておき、
それが何かわかるまでは何も許可しないでください。

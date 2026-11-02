#include "i18n.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

namespace ampify::i18n {

namespace {

// Texts of the kernel and of the services. Simple words: the person reading may not be technical.
constexpr Message kKit[] = {
    {"tray.tooltip", "Ampify"},
    {"tray.previous", "Previous track"},
    {"tray.next", "Next track"},
    {"tray.openKit", "Open the Ampify folder"},
    {"tray.started", "Notification area icon shown"},
    {"tray.failed", "Windows refused the notification area icon ({error})"},
    {"remote.started", "Local remote control ready on http://127.0.0.1:{port} (token required)"},
    {"remote.failed", "Local remote control not started: {error}"},
    {"remote.tokenCreated", "Local remote control: token created in remote.token (ampify config remote.token)"},
    {"discord.connected", "Discord: connected, the presence is shown"},
    {"discord.unavailable", "Discord: client not found or closed, trying again in 15 s"},
    {"discord.error", "Discord refused the presence: {error}"},
    {"midi.opened", "MIDI: input \"{name}\" opened"},
    {"midi.none", "MIDI: no input device found"},
    {"midi.failed", "MIDI: the input cannot be opened ({error})"},
    {"midi.mapRefused", "MIDI: midi.map refused: {error}"},
    {"service.off", "{service}: off (setting {setting} is 0)"},
    {"common.yes", "yes"},
    {"common.no", "no"},
    {"ui.on", "On"},
    {"ui.off", "Off"},
    {"ui.close", "Close"},
    {"page.back", "Back"},
    {"mods.subtitle", "The extensions installed in Ampify. A change applies right away."},
    {"mods.openFolder", "Open the Ampify folder"},
    {"mods.openMarketplace", "Open the Marketplace"},
    {"mods.toggleHelp", "Turns this extension on or off right away"},
    {"mods.themeTitle", "Theme: {name}"},
    {"mods.themeSubtitle", "The settings of the active theme. A change shows at once."},
    {"mods.theme", "Theme"},
    {"mods.themeHelp", "The look of Apple Music. The Marketplace has more."},
    {"mods.themeNone", "None (Apple Music's own look)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "What Ampify changes in how Apple Music behaves. A change applies at once."},
    {"tweaks.notifications", "Notifications"},
    {"tweaks.notificationsHelp", "Such as the one of each new song. Apple's own switch is in Settings, General."},
    {"tweaks.notifications.apple", "As Apple Music shows them"},
    {"tweaks.notifications.background", "Only when Apple Music is not in front"},
    {"tweaks.notifications.never", "Never"},
    {"tweaks.player", "Full-screen player"},
    {"tweaks.playerHelp", "Where Now Playing and Apple's full-screen button open the player."},
    {"tweaks.player.window", "Over the Apple Music window"},
    {"tweaks.player.replace", "In place of the Apple Music window"},
    {"tweaks.player.workArea", "Whole screen, taskbar visible"},
    {"tweaks.player.screen", "Whole screen (Apple's way)"},
    {"tweaks.close", "Closing the window"},
    {"tweaks.closeHelp", "What the window's close button does. In the notification area, the music goes on and the "
                         "icon near the clock brings the window back."},
    {"tweaks.close.quit", "Quits Apple Music"},
    {"tweaks.close.tray", "Keeps it in the notification area"},
    {"tray.show", "Show Apple Music"},
    {"tray.quit", "Quit Apple Music"},
    {"mods.report", "Diagnostic report"},
    {"mods.reportHelp", "A zip of Ampify's state and logs, without your names, folders or music, to attach to an issue."},
    {"mods.report.run", "Export a report"},
    {"mods.report.running", "Writing the report…"},
    {"mods.report.done", "Report exported"},
    {"mods.report.failed", "The report failed"},
    {"settings.entryHelp", "Themes, extensions and the Marketplace: what Ampify adds to Apple Music."},
};

// French texts of what people see (menus, buttons, the Settings card). The log stays in English, so a key missing here
// falls back to the English text.
constexpr Message kKitFr[] = {
    {"tray.previous", "Morceau précédent"},
    {"tray.next", "Morceau suivant"},
    {"tray.openKit", "Ouvrir le dossier Ampify"},
    {"common.yes", "oui"},
    {"common.no", "non"},
    {"ui.on", "Activé"},
    {"ui.off", "Désactivé"},
    {"ui.close", "Fermer"},
    {"page.back", "Retour"},
    {"mods.subtitle", "Les extensions installées dans Ampify. Un changement s'applique tout de suite."},
    {"mods.openFolder", "Ouvrir le dossier Ampify"},
    {"mods.openMarketplace", "Ouvrir le Marketplace"},
    {"mods.toggleHelp", "Active ou désactive cette extension tout de suite"},
    {"mods.themeTitle", "Thème : {name}"},
    {"mods.themeSubtitle", "Les réglages du thème actif. Un changement se voit tout de suite."},
    {"mods.theme", "Thème"},
    {"mods.themeHelp", "L'apparence d'Apple Music. Le Marketplace en propose d'autres."},
    {"mods.themeNone", "Aucun (l'apparence d'Apple Music)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "Ce qu'Ampify change dans le comportement d'Apple Music. Un changement s'applique tout de suite."},
    {"tweaks.notifications", "Notifications"},
    {"tweaks.notificationsHelp", "Comme celle de chaque nouveau morceau. Le réglage d'Apple est dans Réglages, Général."},
    {"tweaks.notifications.apple", "Comme Apple Music les montre"},
    {"tweaks.notifications.background", "Seulement quand Apple Music n'est pas devant"},
    {"tweaks.notifications.never", "Jamais"},
    {"tweaks.player", "Lecteur plein écran"},
    {"tweaks.playerHelp", "Où « À l'écoute » et le bouton plein écran d'Apple ouvrent le lecteur."},
    {"tweaks.player.window", "Par-dessus la fenêtre d'Apple Music"},
    {"tweaks.player.replace", "À la place de la fenêtre d'Apple Music"},
    {"tweaks.player.workArea", "Tout l'écran, barre des tâches visible"},
    {"tweaks.player.screen", "Tout l'écran (comme Apple)"},
    {"tweaks.close", "Fermer la fenêtre"},
    {"tweaks.closeHelp", "Ce que fait la croix de la fenêtre. Dans la zone de notification, la musique continue et "
                         "l'icône près de l'horloge rouvre la fenêtre."},
    {"settings.entryHelp", "Thèmes, extensions et Marketplace : ce qu'Ampify ajoute à Apple Music."},
    {"tweaks.close.quit", "Quitte Apple Music"},
    {"tweaks.close.tray", "La garde dans la zone de notification"},
    {"tray.show", "Afficher Apple Music"},
    {"tray.quit", "Quitter Apple Music"},
    {"mods.report", "Rapport de diagnostic"},
    {"mods.reportHelp", "Un zip de l'état et des journaux d'Ampify, sans vos noms, dossiers ni musique, à joindre à un signalement."},
    {"mods.report.run", "Exporter un rapport"},
    {"mods.report.running", "Rapport en cours…"},
    {"mods.report.done", "Rapport exporté"},
    {"mods.report.failed", "Le rapport a échoué"},
};

constexpr Message kKitDe[] = {
    {"tray.previous", "Vorheriger Titel"},
    {"tray.next", "Nächster Titel"},
    {"tray.openKit", "Ampify-Ordner öffnen"},
    {"tray.show", "Apple Music anzeigen"},
    {"tray.quit", "Apple Music beenden"},
    {"common.yes", "ja"},
    {"common.no", "nein"},
    {"ui.on", "Ein"},
    {"ui.off", "Aus"},
    {"ui.close", "Schließen"},
    {"page.back", "Zurück"},
    {"mods.subtitle", "Die in Ampify installierten Erweiterungen. Jede Änderung gilt sofort."},
    {"mods.openFolder", "Ampify-Ordner öffnen"},
    {"mods.openMarketplace", "Marketplace öffnen"},
    {"mods.toggleHelp", "Schaltet diese Erweiterung sofort ein oder aus"},
    {"mods.themeTitle", "Design: {name}"},
    {"mods.themeSubtitle", "Die Einstellungen des aktiven Designs. Jede Änderung ist sofort sichtbar."},
    {"mods.theme", "Design"},
    {"mods.themeHelp", "Das Aussehen von Apple Music. Im Marketplace gibt es weitere."},
    {"mods.themeNone", "Keins (das Aussehen von Apple Music)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "Was Ampify am Verhalten von Apple Music ändert. Jede Änderung gilt sofort."},
    {"tweaks.notifications", "Benachrichtigungen"},
    {"tweaks.notificationsHelp", "Etwa die zu jedem neuen Titel. Apples eigener Schalter ist unter Einstellungen, Allgemein."},
    {"tweaks.notifications.apple", "So, wie Apple Music sie zeigt"},
    {"tweaks.notifications.background", "Nur wenn Apple Music nicht im Vordergrund ist"},
    {"tweaks.notifications.never", "Nie"},
    {"tweaks.player", "Vollbild-Player"},
    {"tweaks.playerHelp", "Wo „Jetzt läuft“ und Apples Vollbildtaste den Player öffnen."},
    {"tweaks.player.window", "Über dem Apple Music-Fenster"},
    {"tweaks.player.replace", "Anstelle des Apple Music-Fensters"},
    {"tweaks.player.workArea", "Ganzer Bildschirm, Taskleiste sichtbar"},
    {"tweaks.player.screen", "Ganzer Bildschirm (wie bei Apple)"},
    {"tweaks.close", "Fenster schließen"},
    {"tweaks.closeHelp", "Was das Schließen-Kreuz des Fensters tut. Im Infobereich läuft die Musik weiter, und das "
                         "Symbol neben der Uhr holt das Fenster zurück."},
    {"tweaks.close.quit", "Beendet Apple Music"},
    {"tweaks.close.tray", "Behält es im Infobereich"},
    {"mods.report", "Diagnosebericht"},
    {"mods.reportHelp", "Ein Zip mit Zustand und Protokollen von Ampify, ohne Ihre Namen, Ordner oder Musik, zum "
                        "Anhängen an eine Fehlermeldung."},
    {"mods.report.run", "Bericht exportieren"},
    {"mods.report.running", "Bericht wird erstellt…"},
    {"mods.report.done", "Bericht exportiert"},
    {"mods.report.failed", "Der Bericht ist fehlgeschlagen"},
    {"settings.entryHelp", "Designs, Erweiterungen und der Marketplace: was Ampify Apple Music hinzufügt."},
};

constexpr Message kKitEs[] = {
    {"tray.previous", "Canción anterior"},
    {"tray.next", "Canción siguiente"},
    {"tray.openKit", "Abrir la carpeta de Ampify"},
    {"tray.show", "Mostrar Apple Music"},
    {"tray.quit", "Salir de Apple Music"},
    {"common.yes", "sí"},
    {"common.no", "no"},
    {"ui.on", "Activado"},
    {"ui.off", "Desactivado"},
    {"ui.close", "Cerrar"},
    {"page.back", "Atrás"},
    {"mods.subtitle", "Las extensiones instaladas en Ampify. Los cambios se aplican al instante."},
    {"mods.openFolder", "Abrir la carpeta de Ampify"},
    {"mods.openMarketplace", "Abrir el Marketplace"},
    {"mods.toggleHelp", "Activa o desactiva esta extensión al instante"},
    {"mods.themeTitle", "Tema: {name}"},
    {"mods.themeSubtitle", "Los ajustes del tema activo. Los cambios se ven al instante."},
    {"mods.theme", "Tema"},
    {"mods.themeHelp", "El aspecto de Apple Music. En el Marketplace hay más."},
    {"mods.themeNone", "Ninguno (el aspecto de Apple Music)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "Lo que Ampify cambia en el comportamiento de Apple Music. Los cambios se aplican al instante."},
    {"tweaks.notifications", "Notificaciones"},
    {"tweaks.notificationsHelp", "Como la de cada canción nueva. El interruptor de Apple está en Ajustes, General."},
    {"tweaks.notifications.apple", "Como las muestra Apple Music"},
    {"tweaks.notifications.background", "Solo cuando Apple Music no está en primer plano"},
    {"tweaks.notifications.never", "Nunca"},
    {"tweaks.player", "Reproductor a pantalla completa"},
    {"tweaks.playerHelp", "Dónde abren el reproductor «Reproduciendo» y el botón de pantalla completa de Apple."},
    {"tweaks.player.window", "Sobre la ventana de Apple Music"},
    {"tweaks.player.replace", "En lugar de la ventana de Apple Music"},
    {"tweaks.player.workArea", "Toda la pantalla, con la barra de tareas visible"},
    {"tweaks.player.screen", "Toda la pantalla (como Apple)"},
    {"tweaks.close", "Al cerrar la ventana"},
    {"tweaks.closeHelp", "Lo que hace la X de la ventana. En el área de notificación, la música sigue sonando y el "
                         "icono junto al reloj vuelve a abrir la ventana."},
    {"tweaks.close.quit", "Sale de Apple Music"},
    {"tweaks.close.tray", "Lo deja en el área de notificación"},
    {"mods.report", "Informe de diagnóstico"},
    {"mods.reportHelp", "Un zip con el estado y los registros de Ampify, sin tus nombres, carpetas ni música, para "
                        "adjuntar a un aviso de error."},
    {"mods.report.run", "Exportar un informe"},
    {"mods.report.running", "Creando el informe…"},
    {"mods.report.done", "Informe exportado"},
    {"mods.report.failed", "No se pudo crear el informe"},
    {"settings.entryHelp", "Temas, extensiones y el Marketplace: lo que Ampify añade a Apple Music."},
};

constexpr Message kKitPt[] = {
    {"tray.previous", "Faixa anterior"},
    {"tray.next", "Próxima faixa"},
    {"tray.openKit", "Abrir a pasta do Ampify"},
    {"tray.show", "Mostrar o Apple Music"},
    {"tray.quit", "Sair do Apple Music"},
    {"common.yes", "sim"},
    {"common.no", "não"},
    {"ui.on", "Ativado"},
    {"ui.off", "Desativado"},
    {"ui.close", "Fechar"},
    {"page.back", "Voltar"},
    {"mods.subtitle", "As extensões instaladas no Ampify. As mudanças valem na hora."},
    {"mods.openFolder", "Abrir a pasta do Ampify"},
    {"mods.openMarketplace", "Abrir o Marketplace"},
    {"mods.toggleHelp", "Liga ou desliga esta extensão na hora"},
    {"mods.themeTitle", "Tema: {name}"},
    {"mods.themeSubtitle", "Os ajustes do tema ativo. As mudanças aparecem na hora."},
    {"mods.theme", "Tema"},
    {"mods.themeHelp", "A aparência do Apple Music. O Marketplace tem outros."},
    {"mods.themeNone", "Nenhum (a aparência do Apple Music)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "O que o Ampify muda no comportamento do Apple Music. As mudanças valem na hora."},
    {"tweaks.notifications", "Notificações"},
    {"tweaks.notificationsHelp", "Como a de cada nova música. A opção da Apple fica em Ajustes, Geral."},
    {"tweaks.notifications.apple", "Como o Apple Music as mostra"},
    {"tweaks.notifications.background", "Só quando o Apple Music não está em primeiro plano"},
    {"tweaks.notifications.never", "Nunca"},
    {"tweaks.player", "Player em tela cheia"},
    {"tweaks.playerHelp", "Onde o \"Tocando agora\" e o botão de tela cheia da Apple abrem o player."},
    {"tweaks.player.window", "Sobre a janela do Apple Music"},
    {"tweaks.player.replace", "No lugar da janela do Apple Music"},
    {"tweaks.player.workArea", "Tela inteira, com a barra de tarefas visível"},
    {"tweaks.player.screen", "Tela inteira (como a Apple)"},
    {"tweaks.close", "Ao fechar a janela"},
    {"tweaks.closeHelp", "O que o X da janela faz. Na área de notificação, a música continua e o ícone perto do "
                         "relógio reabre a janela."},
    {"tweaks.close.quit", "Sai do Apple Music"},
    {"tweaks.close.tray", "Mantém na área de notificação"},
    {"mods.report", "Relatório de diagnóstico"},
    {"mods.reportHelp", "Um zip com o estado e os registros do Ampify, sem seus nomes, pastas ou músicas, para anexar "
                        "a um relato de problema."},
    {"mods.report.run", "Exportar um relatório"},
    {"mods.report.running", "Gerando o relatório…"},
    {"mods.report.done", "Relatório exportado"},
    {"mods.report.failed", "O relatório falhou"},
    {"settings.entryHelp", "Temas, extensões e o Marketplace: o que o Ampify acrescenta ao Apple Music."},
};

constexpr Message kKitRu[] = {
    {"tray.previous", "Предыдущий трек"},
    {"tray.next", "Следующий трек"},
    {"tray.openKit", "Открыть папку Ampify"},
    {"tray.show", "Показать Apple Music"},
    {"tray.quit", "Выйти из Apple Music"},
    {"common.yes", "да"},
    {"common.no", "нет"},
    {"ui.on", "Вкл."},
    {"ui.off", "Выкл."},
    {"ui.close", "Закрыть"},
    {"page.back", "Назад"},
    {"mods.subtitle", "Расширения, установленные в Ampify. Изменения применяются сразу."},
    {"mods.openFolder", "Открыть папку Ampify"},
    {"mods.openMarketplace", "Открыть Marketplace"},
    {"mods.toggleHelp", "Сразу включает или выключает это расширение"},
    {"mods.themeTitle", "Тема: {name}"},
    {"mods.themeSubtitle", "Настройки текущей темы. Изменения видны сразу."},
    {"mods.theme", "Тема"},
    {"mods.themeHelp", "Внешний вид Apple Music. Другие темы есть в Marketplace."},
    {"mods.themeNone", "Нет (стандартный вид Apple Music)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "Что Ampify меняет в поведении Apple Music. Изменения применяются сразу."},
    {"tweaks.notifications", "Уведомления"},
    {"tweaks.notificationsHelp", "Например, о каждом новом треке. Переключатель самой Apple — в Настройках, раздел "
                                 "«Основные»."},
    {"tweaks.notifications.apple", "Как их показывает Apple Music"},
    {"tweaks.notifications.background", "Только когда Apple Music не на переднем плане"},
    {"tweaks.notifications.never", "Никогда"},
    {"tweaks.player", "Полноэкранный плеер"},
    {"tweaks.playerHelp", "Где «Сейчас играет» и кнопка полноэкранного режима Apple открывают плеер."},
    {"tweaks.player.window", "Поверх окна Apple Music"},
    {"tweaks.player.replace", "Вместо окна Apple Music"},
    {"tweaks.player.workArea", "Весь экран, панель задач видна"},
    {"tweaks.player.screen", "Весь экран (как у Apple)"},
    {"tweaks.close", "Закрытие окна"},
    {"tweaks.closeHelp", "Что делает крестик окна. В области уведомлений музыка продолжает играть, а значок у часов "
                         "снова открывает окно."},
    {"tweaks.close.quit", "Закрывает Apple Music"},
    {"tweaks.close.tray", "Сворачивает в область уведомлений"},
    {"mods.report", "Диагностический отчёт"},
    {"mods.reportHelp", "Zip-архив с состоянием и журналами Ampify, без ваших имён, папок и музыки, чтобы приложить к "
                        "сообщению о проблеме."},
    {"mods.report.run", "Экспортировать отчёт"},
    {"mods.report.running", "Создание отчёта…"},
    {"mods.report.done", "Отчёт экспортирован"},
    {"mods.report.failed", "Не удалось создать отчёт"},
    {"settings.entryHelp", "Темы, расширения и Marketplace: всё, что Ampify добавляет в Apple Music."},
};

constexpr Message kKitJa[] = {
    {"tray.previous", "前の曲"},
    {"tray.next", "次の曲"},
    {"tray.openKit", "Ampify フォルダーを開く"},
    {"tray.show", "Apple Music を表示"},
    {"tray.quit", "Apple Music を終了"},
    {"common.yes", "はい"},
    {"common.no", "いいえ"},
    {"ui.on", "オン"},
    {"ui.off", "オフ"},
    {"ui.close", "閉じる"},
    {"page.back", "戻る"},
    {"mods.subtitle", "Ampify にインストールされている拡張機能です。変更はすぐに反映されます。"},
    {"mods.openFolder", "Ampify フォルダーを開く"},
    {"mods.openMarketplace", "Marketplace を開く"},
    {"mods.toggleHelp", "この拡張機能をすぐにオン・オフします"},
    {"mods.themeTitle", "テーマ: {name}"},
    {"mods.themeSubtitle", "使用中のテーマの設定です。変更はすぐに表示されます。"},
    {"mods.theme", "テーマ"},
    {"mods.themeHelp", "Apple Music の見た目です。Marketplace にほかのテーマもあります。"},
    {"mods.themeNone", "なし (Apple Music 標準の見た目)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "Ampify が Apple Music の動作について変更する項目です。変更はすぐに反映されます。"},
    {"tweaks.notifications", "通知"},
    {"tweaks.notificationsHelp", "新しい曲ごとの通知など。Apple 自体のスイッチは「設定」の「一般」にあります。"},
    {"tweaks.notifications.apple", "Apple Music の表示どおり"},
    {"tweaks.notifications.background", "Apple Music が前面にないときだけ"},
    {"tweaks.notifications.never", "表示しない"},
    {"tweaks.player", "フルスクリーンプレーヤー"},
    {"tweaks.playerHelp", "「再生中」と Apple のフルスクリーンボタンがプレーヤーを開く場所です。"},
    {"tweaks.player.window", "Apple Music のウインドウの上"},
    {"tweaks.player.replace", "Apple Music のウインドウの代わり"},
    {"tweaks.player.workArea", "画面全体 (タスクバーは表示)"},
    {"tweaks.player.screen", "画面全体 (Apple と同じ)"},
    {"tweaks.close", "ウインドウを閉じたとき"},
    {"tweaks.closeHelp", "ウインドウの閉じるボタンの動作です。通知領域にある間も音楽は流れ続け、時計の近くのアイコンで"
                         "ウインドウを再表示できます。"},
    {"tweaks.close.quit", "Apple Music を終了する"},
    {"tweaks.close.tray", "通知領域に残す"},
    {"mods.report", "診断レポート"},
    {"mods.reportHelp", "Ampify の状態とログをまとめた zip です。名前、フォルダー、音楽は含まれません。問題の報告に"
                        "添付してください。"},
    {"mods.report.run", "レポートを書き出す"},
    {"mods.report.running", "レポートを作成中…"},
    {"mods.report.done", "レポートを書き出しました"},
    {"mods.report.failed", "レポートを作成できませんでした"},
    {"settings.entryHelp", "テーマ、拡張機能、Marketplace: Ampify が Apple Music に加える機能です。"},
};

constexpr Message kKitZh[] = {
    {"tray.previous", "上一首"},
    {"tray.next", "下一首"},
    {"tray.openKit", "打开 Ampify 文件夹"},
    {"tray.show", "显示 Apple Music"},
    {"tray.quit", "退出 Apple Music"},
    {"common.yes", "是"},
    {"common.no", "否"},
    {"ui.on", "开"},
    {"ui.off", "关"},
    {"ui.close", "关闭"},
    {"page.back", "返回"},
    {"mods.subtitle", "Ampify 中已安装的扩展。更改会立即生效。"},
    {"mods.openFolder", "打开 Ampify 文件夹"},
    {"mods.openMarketplace", "打开 Marketplace"},
    {"mods.toggleHelp", "立即打开或关闭此扩展"},
    {"mods.themeTitle", "主题：{name}"},
    {"mods.themeSubtitle", "当前主题的设置。更改会立即显示。"},
    {"mods.theme", "主题"},
    {"mods.themeHelp", "Apple Music 的外观。Marketplace 里还有更多主题。"},
    {"mods.themeNone", "无（Apple Music 默认外观）"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "Ampify 对 Apple Music 行为的更改。更改会立即生效。"},
    {"tweaks.notifications", "通知"},
    {"tweaks.notificationsHelp", "例如每首新歌的通知。Apple 自带的开关在“设置”的“通用”中。"},
    {"tweaks.notifications.apple", "按 Apple Music 的方式显示"},
    {"tweaks.notifications.background", "仅当 Apple Music 不在前台时"},
    {"tweaks.notifications.never", "从不"},
    {"tweaks.player", "全屏播放器"},
    {"tweaks.playerHelp", "“正在播放”和 Apple 的全屏按钮在哪里打开播放器。"},
    {"tweaks.player.window", "覆盖在 Apple Music 窗口上"},
    {"tweaks.player.replace", "取代 Apple Music 窗口"},
    {"tweaks.player.workArea", "整个屏幕，保留任务栏"},
    {"tweaks.player.screen", "整个屏幕（与 Apple 相同）"},
    {"tweaks.close", "关闭窗口时"},
    {"tweaks.closeHelp", "窗口关闭按钮的作用。在通知区域中音乐会继续播放，点击时钟旁的图标即可重新打开窗口。"},
    {"tweaks.close.quit", "退出 Apple Music"},
    {"tweaks.close.tray", "保留在通知区域"},
    {"mods.report", "诊断报告"},
    {"mods.reportHelp", "包含 Ampify 状态和日志的 zip 文件，不含你的姓名、文件夹或音乐，可附在问题报告中。"},
    {"mods.report.run", "导出报告"},
    {"mods.report.running", "正在生成报告…"},
    {"mods.report.done", "报告已导出"},
    {"mods.report.failed", "报告生成失败"},
    {"settings.entryHelp", "主题、扩展和 Marketplace：Ampify 为 Apple Music 增加的内容。"},
};

struct LanguageTable {
    std::string_view code;
    std::span<const Message> messages;
};

constexpr LanguageTable kLanguages[] = {
    {"fr", kKitFr}, {"de", kKitDe}, {"es", kKitEs}, {"pt", kKitPt}, {"ru", kKitRu}, {"ja", kKitJa}, {"zh", kKitZh},
};

std::string g_language = "en";  // set once at start, before any service reads it

std::optional<std::string_view> Find(std::span<const Message> table, std::string_view key) {
    for (const Message& message : table) {
        if (message.key == key) return message.text;
    }
    return std::nullopt;
}

bool IsLetter(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

// Taiwan, Hong Kong and Macau write Traditional Chinese: they get "zh-hant", which has no table yet, so English
// rather than the Simplified texts of "zh". `locale` is in lower case.
bool IsTraditionalChinese(std::string_view locale) {
    for (std::string_view mark : {"hant", "-tw", "_tw", "-hk", "_hk", "-mo", "_mo"}) {
        if (locale.find(mark) != std::string_view::npos) return true;
    }
    return false;
}

}  // namespace

std::string_view Lookup(std::span<const Message> table, std::string_view key) {
    return Find(table, key).value_or(key);
}

std::string Format(std::string_view pattern, Args args) {
    std::string out;
    out.reserve(pattern.size());
    for (size_t i = 0; i < pattern.size(); i++) {
        if (pattern[i] == '{') {
            size_t close = pattern.find('}', i + 1);
            if (close != std::string_view::npos) {
                std::string_view name = pattern.substr(i + 1, close - i - 1);
                bool replaced = false;
                for (const auto& [key, value] : args) {
                    if (key == name) {
                        out.append(value);
                        replaced = true;
                        break;
                    }
                }
                if (replaced) {
                    i = close;
                    continue;
                }
            }
        }
        out.push_back(pattern[i]);
    }
    return out;
}

std::string Placeholders(std::string_view pattern) {
    std::string names;
    for (size_t i = 0; i < pattern.size(); i++) {
        if (pattern[i] != '{') continue;
        size_t close = pattern.find('}', i + 1);
        if (close == std::string_view::npos) break;
        std::string_view name = pattern.substr(i + 1, close - i - 1);
        bool word = !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        });
        if (word) {
            names += (names.empty() ? "" : ",") + std::string(name);
            i = close;
        }
    }
    return names;
}

std::span<const Message> KitMessages() { return kKit; }

std::span<const Message> KitMessagesIn(std::string_view language) {
    for (const LanguageTable& table : kLanguages) {
        if (table.code == language) return table.messages;
    }
    return kKit;
}

void SetLanguage(std::string_view code) { g_language = std::string(code); }

const std::string& Language() { return g_language; }

bool UsesDecimalComma() {
    for (std::string_view code : {"fr", "de", "es", "pt", "ru"}) {
        if (g_language == code) return true;
    }
    return false;
}

std::string Text(std::string_view key, Args args) {
    std::optional<std::string_view> text = Find(KitMessagesIn(g_language), key);
    return Format(text ? *text : Lookup(KitMessages(), key), args);
}

std::string LanguageCode(std::string_view locale) {
    bool twoLetters = locale.size() >= 2 && IsLetter(locale[0]) && IsLetter(locale[1]) &&
                      (locale.size() == 2 || !IsLetter(locale[2]));
    if (!twoLetters) return "en";
    std::string lower(locale);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    std::string code = lower.substr(0, 2);
    if (code == "zh" && IsTraditionalChinese(lower)) return "zh-hant";
    return code;
}

}  // namespace ampify::i18n

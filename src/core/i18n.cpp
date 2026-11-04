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
    {"ui.on", "On"},
    {"ui.off", "Off"},
    {"ui.close", "Close"},
    {"page.back", "Back"},
    {"mods.subtitle", "Themes, extensions and what Ampify changes in Apple Music. A change applies right away."},
    {"mods.openFolder", "Open the Ampify folder"},
    {"mods.openMarketplace", "Open the Marketplace"},
    {"mods.toggleHelp", "Turns this extension on or off right away"},
    {"mods.themeTitle", "Theme: {name}"},
    {"mods.themeSubtitle", "The settings of the active theme. A change shows at once."},
    {"mods.snippetTitle", "Snippet: {name}"},
    {"mods.snippetSubtitle", "The settings of a snippet switched on. A change shows at once."},
    {"mods.theme", "Theme"},
    {"mods.themeHelp", "The look of Apple Music. The Marketplace has more."},
    {"mods.themeNone", "None (Apple Music's own look)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "What Ampify changes in how Apple Music behaves. The rest is in Apple's own settings."},
    {"tweaks.notifications", "Song notifications"},
    {"tweaks.notificationsHelp", "Apple Music shows one for each new song. Apple's own switch turns them off (Settings, General); Ampify can keep them quiet while Apple Music is in front."},
    {"tweaks.notifications.apple", "Always, as Apple Music does"},
    {"tweaks.notifications.background", "Only when Apple Music is not in front"},
    {"tweaks.notifications.never", "Never"},
    {"tweaks.player", "Full-screen player"},
    {"tweaks.playerHelp", "Where the player opens. Escape or its close button brings Apple Music back."},
    {"tweaks.player.window", "Over Apple Music, as a second window"},
    {"tweaks.player.replace", "In the Apple Music window (one window)"},
    {"tweaks.player.workArea", "Whole screen, taskbar visible"},
    {"tweaks.player.screen", "Whole screen, taskbar hidden (Apple's way)"},
    {"tweaks.playerControls", "Shuffle and repeat in the player"},
    {"tweaks.playerControlsHelp", "Adds Apple Music's shuffle and repeat buttons around the full-screen player's own."},
    {"tweaks.playerControls.shown", "Shown"},
    {"tweaks.playerControls.hidden", "Hidden"},
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
    {"mods.extensions", "Extensions"},
    {"mods.extensionsHelp", "Turn each one on or off. The Marketplace has more."},
    {"mods.none", "No extension installed yet. The Marketplace has some."},
    {"sound.title", "Sound"},
    {"sound.leveler", "Level Dolby Atmos songs"},
    {"sound.levelerHelp", "A Dolby Atmos song often plays quieter than the others. Ampify brings it up to the level of stereo songs, which stay as they are."},
    {"mods.look", "Look"},
    {"mods.help", "Help"},
    {"mods.folder", "Ampify folder"},
    {"mods.folderHelp", "Where themes, extensions, settings and logs are kept."},
    {"accent.title", "Accent colour"},
    {"accent.help", "The colour of buttons, links and selections. Over any theme, or Apple Music's own look."},
    {"accent.theme", "The theme's own"},
    {"accent.custom", "Custom…"},
    {"accent.pink", "Apple Music pink"},
    {"accent.green", "Green"},
    {"accent.blue", "Blue"},
    {"accent.purple", "Purple"},
    {"accent.orange", "Orange"},
    {"update.title", "Ampify updates"},
    {"update.help", "New versions of Ampify, checked once a day while Apple Music is closed."},
    {"update.auto", "Install them on their own"},
    {"update.ask", "Ask me first"},
    {"update.off", "Never"},
    {"update.askTitle", "Ampify {version} is ready"},
    {"update.askText", "A new version of Ampify is available (you have {installed}). Install it now? It takes a few seconds, while Apple Music is closed."},
    {"update.doneTitle", "Ampify updated"},
    {"update.doneText", "Ampify {version} is installed. It loads the next time Apple Music opens."},
};

// French texts of what people see (menus, buttons, the Settings card). The log stays in English, so a key missing here
// falls back to the English text.
constexpr Message kKitFr[] = {
    {"tray.previous", "Morceau précédent"},
    {"tray.next", "Morceau suivant"},
    {"tray.openKit", "Ouvrir le dossier Ampify"},
    {"ui.on", "Activé"},
    {"ui.off", "Désactivé"},
    {"ui.close", "Fermer"},
    {"page.back", "Retour"},
    {"mods.subtitle", "Thèmes, extensions et ce qu'Ampify change dans Apple Music. Un changement s'applique tout de suite."},
    {"mods.openFolder", "Ouvrir le dossier Ampify"},
    {"mods.openMarketplace", "Ouvrir le Marketplace"},
    {"mods.toggleHelp", "Active ou désactive cette extension tout de suite"},
    {"mods.themeTitle", "Thème : {name}"},
    {"mods.themeSubtitle", "Les réglages du thème actif. Un changement se voit tout de suite."},
    {"mods.snippetTitle", "Snippet : {name}"},
    {"mods.snippetSubtitle", "Les réglages d'un snippet activé. Un changement se voit tout de suite."},
    {"mods.theme", "Thème"},
    {"mods.themeHelp", "L'apparence d'Apple Music. Le Marketplace en propose d'autres."},
    {"mods.themeNone", "Aucun (l'apparence d'Apple Music)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "Ce qu'Ampify change dans le comportement d'Apple Music. Le reste se règle dans les réglages d'Apple."},
    {"tweaks.notifications", "Notifications de morceau"},
    {"tweaks.notificationsHelp", "Apple Music en montre une à chaque nouveau morceau. Le réglage d'Apple les coupe (Réglages, Général) ; Ampify peut les taire quand Apple Music est devant vous."},
    {"tweaks.notifications.apple", "Toujours, comme Apple Music"},
    {"tweaks.notifications.background", "Seulement quand Apple Music n'est pas devant"},
    {"tweaks.notifications.never", "Jamais"},
    {"tweaks.player", "Lecteur plein écran"},
    {"tweaks.playerHelp", "Où s'ouvre le lecteur. Échap ou sa croix ramène Apple Music."},
    {"tweaks.player.window", "Par-dessus Apple Music, dans une seconde fenêtre"},
    {"tweaks.player.replace", "Dans la fenêtre d'Apple Music (une seule fenêtre)"},
    {"tweaks.player.workArea", "Tout l'écran, barre des tâches visible"},
    {"tweaks.player.screen", "Tout l'écran, barre des tâches cachée (comme Apple)"},
    {"tweaks.playerControls", "Aléatoire et répétition dans le lecteur"},
    {"tweaks.playerControlsHelp", "Ajoute les boutons aléatoire et répétition d'Apple Music autour de ceux du lecteur plein écran."},
    {"tweaks.playerControls.shown", "Affichés"},
    {"tweaks.playerControls.hidden", "Masqués"},
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
    {"mods.extensions", "Extensions"},
    {"mods.extensionsHelp", "Activez ou désactivez chacune. Le Marketplace en propose d'autres."},
    {"mods.none", "Aucune extension installée pour l'instant. Le Marketplace en propose."},
    {"sound.title", "Son"},
    {"sound.leveler", "Égaliser le volume des titres Dolby Atmos"},
    {"sound.levelerHelp", "Un titre Dolby Atmos sonne souvent plus bas que les autres. Ampify le remonte au niveau des titres stéréo, qui restent tels quels."},
    {"mods.look", "Apparence"},
    {"mods.help", "Aide"},
    {"mods.folder", "Dossier Ampify"},
    {"mods.folderHelp", "Là où sont gardés thèmes, extensions, réglages et journaux."},
    {"accent.title", "Couleur d'accent"},
    {"accent.help", "La couleur des boutons, des liens et des sélections. Par-dessus n'importe quel thème, ou l'apparence d'Apple Music."},
    {"accent.theme", "Celle du thème"},
    {"accent.custom", "Personnalisée…"},
    {"accent.pink", "Rose Apple Music"},
    {"accent.green", "Vert"},
    {"accent.blue", "Bleu"},
    {"accent.purple", "Violet"},
    {"accent.orange", "Orange"},
    {"update.title", "Mises à jour d'Ampify"},
    {"update.help", "Les nouvelles versions d'Ampify, cherchées une fois par jour quand Apple Music est fermé."},
    {"update.auto", "Les installer toutes seules"},
    {"update.ask", "Me demander d'abord"},
    {"update.off", "Jamais"},
    {"update.askTitle", "Ampify {version} est prêt"},
    {"update.askText", "Une nouvelle version d'Ampify est disponible (vous avez la {installed}). L'installer maintenant ? Cela prend quelques secondes, pendant qu'Apple Music est fermé."},
    {"update.doneTitle", "Ampify mis à jour"},
    {"update.doneText", "Ampify {version} est installé. Il se charge à la prochaine ouverture d'Apple Music."},
};

constexpr Message kKitDe[] = {
    {"tray.previous", "Vorheriger Titel"},
    {"tray.next", "Nächster Titel"},
    {"tray.openKit", "Ampify-Ordner öffnen"},
    {"tray.show", "Apple Music anzeigen"},
    {"tray.quit", "Apple Music beenden"},
    {"ui.on", "Ein"},
    {"ui.off", "Aus"},
    {"ui.close", "Schließen"},
    {"page.back", "Zurück"},
    {"mods.subtitle", "Designs, Erweiterungen und was Ampify an Apple Music ändert. Jede Änderung gilt sofort."},
    {"mods.openFolder", "Ampify-Ordner öffnen"},
    {"mods.openMarketplace", "Marketplace öffnen"},
    {"mods.toggleHelp", "Schaltet diese Erweiterung sofort ein oder aus"},
    {"mods.themeTitle", "Design: {name}"},
    {"mods.themeSubtitle", "Die Einstellungen des aktiven Designs. Jede Änderung ist sofort sichtbar."},
    {"mods.snippetTitle", "Snippet: {name}"},
    {"mods.snippetSubtitle", "Die Einstellungen eines eingeschalteten Snippets. Jede Änderung ist sofort sichtbar."},
    {"mods.theme", "Design"},
    {"mods.themeHelp", "Das Aussehen von Apple Music. Im Marketplace gibt es weitere."},
    {"mods.themeNone", "Keins (das Aussehen von Apple Music)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "Was Ampify am Verhalten von Apple Music ändert. Alles andere steht in den Einstellungen von Apple."},
    {"tweaks.notifications", "Titelbenachrichtigungen"},
    {"tweaks.notificationsHelp", "Apple Music zeigt bei jedem neuen Titel eine an. Apples eigener Schalter stellt sie ab (Einstellungen, Allgemein); Ampify kann sie stummschalten, solange Apple Music vorne ist."},
    {"tweaks.notifications.apple", "Immer, wie Apple Music"},
    {"tweaks.notifications.background", "Nur wenn Apple Music nicht vorne ist"},
    {"tweaks.notifications.never", "Nie"},
    {"tweaks.player", "Vollbild-Player"},
    {"tweaks.playerHelp", "Wo sich der Player öffnet. Esc oder sein Schließen-Knopf holt Apple Music zurück."},
    {"tweaks.player.window", "Über Apple Music, in einem zweiten Fenster"},
    {"tweaks.player.replace", "Im Fenster von Apple Music (ein einziges Fenster)"},
    {"tweaks.player.workArea", "Ganzer Bildschirm, Taskleiste sichtbar"},
    {"tweaks.player.screen", "Ganzer Bildschirm, Taskleiste verdeckt (wie bei Apple)"},
    {"tweaks.playerControls", "Zufall und Wiederholen im Player"},
    {"tweaks.playerControlsHelp", "Fügt die Zufalls- und Wiederholen-Tasten von Apple Music neben die des Vollbild-Players."},
    {"tweaks.playerControls.shown", "Angezeigt"},
    {"tweaks.playerControls.hidden", "Ausgeblendet"},
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
    {"mods.extensions", "Erweiterungen"},
    {"mods.extensionsHelp", "Schalten Sie jede einzeln ein oder aus. Im Marketplace gibt es weitere."},
    {"mods.none", "Noch keine Erweiterung installiert. Der Marketplace bietet welche an."},
    {"sound.title", "Ton"},
    {"sound.leveler", "Lautstärke von Dolby-Atmos-Titeln angleichen"},
    {"sound.levelerHelp", "Ein Dolby-Atmos-Titel klingt oft leiser als die anderen. Ampify hebt ihn auf die Lautstärke der Stereotitel an, die bleiben, wie sie sind."},
    {"mods.look", "Darstellung"},
    {"mods.help", "Hilfe"},
    {"mods.folder", "Ampify-Ordner"},
    {"mods.folderHelp", "Hier liegen Designs, Erweiterungen, Einstellungen und Protokolle."},
    {"accent.title", "Akzentfarbe"},
    {"accent.help", "Die Farbe von Schaltflächen, Links und Auswahlen. Über jedem Design oder dem Aussehen von Apple Music."},
    {"accent.theme", "Die des Designs"},
    {"accent.custom", "Eigene…"},
    {"accent.pink", "Apple-Music-Pink"},
    {"accent.green", "Grün"},
    {"accent.blue", "Blau"},
    {"accent.purple", "Violett"},
    {"accent.orange", "Orange"},
    {"update.title", "Ampify-Updates"},
    {"update.help", "Neue Versionen von Ampify, einmal am Tag gesucht, während Apple Music geschlossen ist."},
    {"update.auto", "Selbst installieren"},
    {"update.ask", "Vorher fragen"},
    {"update.off", "Nie"},
    {"update.askTitle", "Ampify {version} ist bereit"},
    {"update.askText", "Eine neue Version von Ampify ist verfügbar (installiert: {installed}). Jetzt installieren? Das dauert ein paar Sekunden, solange Apple Music geschlossen ist."},
    {"update.doneTitle", "Ampify aktualisiert"},
    {"update.doneText", "Ampify {version} ist installiert. Es lädt beim nächsten Start von Apple Music."},
};

constexpr Message kKitEs[] = {
    {"tray.previous", "Canción anterior"},
    {"tray.next", "Canción siguiente"},
    {"tray.openKit", "Abrir la carpeta de Ampify"},
    {"tray.show", "Mostrar Apple Music"},
    {"tray.quit", "Salir de Apple Music"},
    {"ui.on", "Activado"},
    {"ui.off", "Desactivado"},
    {"ui.close", "Cerrar"},
    {"page.back", "Atrás"},
    {"mods.subtitle", "Temas, extensiones y lo que Ampify cambia en Apple Music. Los cambios se aplican al instante."},
    {"mods.openFolder", "Abrir la carpeta de Ampify"},
    {"mods.openMarketplace", "Abrir el Marketplace"},
    {"mods.toggleHelp", "Activa o desactiva esta extensión al instante"},
    {"mods.themeTitle", "Tema: {name}"},
    {"mods.themeSubtitle", "Los ajustes del tema activo. Los cambios se ven al instante."},
    {"mods.snippetTitle", "Snippet: {name}"},
    {"mods.snippetSubtitle", "Los ajustes de un snippet activado. Los cambios se ven al instante."},
    {"mods.theme", "Tema"},
    {"mods.themeHelp", "El aspecto de Apple Music. En el Marketplace hay más."},
    {"mods.themeNone", "Ninguno (el aspecto de Apple Music)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "Lo que Ampify cambia en el comportamiento de Apple Music. El resto está en los ajustes de Apple."},
    {"tweaks.notifications", "Notificaciones de canción"},
    {"tweaks.notificationsHelp", "Apple Music muestra una con cada canción nueva. El ajuste de Apple las desactiva (Ajustes, General); Ampify puede silenciarlas mientras Apple Music está delante."},
    {"tweaks.notifications.apple", "Siempre, como Apple Music"},
    {"tweaks.notifications.background", "Solo cuando Apple Music no está delante"},
    {"tweaks.notifications.never", "Nunca"},
    {"tweaks.player", "Reproductor a pantalla completa"},
    {"tweaks.playerHelp", "Dónde se abre el reproductor. Esc o su botón de cerrar vuelve a Apple Music."},
    {"tweaks.player.window", "Sobre Apple Music, en una segunda ventana"},
    {"tweaks.player.replace", "En la ventana de Apple Music (una sola ventana)"},
    {"tweaks.player.workArea", "Toda la pantalla, con la barra de tareas visible"},
    {"tweaks.player.screen", "Toda la pantalla, sin la barra de tareas (como Apple)"},
    {"tweaks.playerControls", "Aleatorio y repetir en el reproductor"},
    {"tweaks.playerControlsHelp", "Añade los botones aleatorio y repetir de Apple Music junto a los del reproductor a pantalla completa."},
    {"tweaks.playerControls.shown", "Visibles"},
    {"tweaks.playerControls.hidden", "Ocultos"},
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
    {"mods.extensions", "Extensiones"},
    {"mods.extensionsHelp", "Activa o desactiva cada una. En el Marketplace hay más."},
    {"mods.none", "Todavía no hay ninguna extensión instalada. El Marketplace ofrece algunas."},
    {"sound.title", "Sonido"},
    {"sound.leveler", "Igualar el volumen de las canciones Dolby Atmos"},
    {"sound.levelerHelp", "Una canción Dolby Atmos suele sonar más baja que las demás. Ampify la sube al nivel de las canciones estéreo, que se quedan como están."},
    {"mods.look", "Aspecto"},
    {"mods.help", "Ayuda"},
    {"mods.folder", "Carpeta de Ampify"},
    {"mods.folderHelp", "Donde se guardan temas, extensiones, ajustes y registros."},
    {"accent.title", "Color de acento"},
    {"accent.help", "El color de botones, enlaces y selecciones. Sobre cualquier tema o sobre el aspecto de Apple Music."},
    {"accent.theme", "El del tema"},
    {"accent.custom", "Personalizado…"},
    {"accent.pink", "Rosa Apple Music"},
    {"accent.green", "Verde"},
    {"accent.blue", "Azul"},
    {"accent.purple", "Morado"},
    {"accent.orange", "Naranja"},
    {"update.title", "Actualizaciones de Ampify"},
    {"update.help", "Las nuevas versiones de Ampify, buscadas una vez al día con Apple Music cerrado."},
    {"update.auto", "Instalarlas solas"},
    {"update.ask", "Preguntarme antes"},
    {"update.off", "Nunca"},
    {"update.askTitle", "Ampify {version} está lista"},
    {"update.askText", "Hay una nueva versión de Ampify (tienes la {installed}). ¿Instalarla ahora? Tarda unos segundos, con Apple Music cerrado."},
    {"update.doneTitle", "Ampify actualizado"},
    {"update.doneText", "Ampify {version} está instalado. Se carga la próxima vez que se abra Apple Music."},
};

constexpr Message kKitPt[] = {
    {"tray.previous", "Faixa anterior"},
    {"tray.next", "Próxima faixa"},
    {"tray.openKit", "Abrir a pasta do Ampify"},
    {"tray.show", "Mostrar o Apple Music"},
    {"tray.quit", "Sair do Apple Music"},
    {"ui.on", "Ativado"},
    {"ui.off", "Desativado"},
    {"ui.close", "Fechar"},
    {"page.back", "Voltar"},
    {"mods.subtitle", "Temas, extensões e o que o Ampify muda no Apple Music. As mudanças valem na hora."},
    {"mods.openFolder", "Abrir a pasta do Ampify"},
    {"mods.openMarketplace", "Abrir o Marketplace"},
    {"mods.toggleHelp", "Liga ou desliga esta extensão na hora"},
    {"mods.themeTitle", "Tema: {name}"},
    {"mods.themeSubtitle", "Os ajustes do tema ativo. As mudanças aparecem na hora."},
    {"mods.snippetTitle", "Snippet: {name}"},
    {"mods.snippetSubtitle", "Os ajustes de um snippet ativado. As mudanças aparecem na hora."},
    {"mods.theme", "Tema"},
    {"mods.themeHelp", "A aparência do Apple Music. O Marketplace tem outros."},
    {"mods.themeNone", "Nenhum (a aparência do Apple Music)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "O que o Ampify muda no comportamento do Apple Music. O resto fica nos ajustes da Apple."},
    {"tweaks.notifications", "Notificações de música"},
    {"tweaks.notificationsHelp", "O Apple Music mostra uma a cada música nova. O ajuste da Apple as desativa (Ajustes, Geral); o Ampify pode silenciá-las enquanto o Apple Music está na frente."},
    {"tweaks.notifications.apple", "Sempre, como o Apple Music"},
    {"tweaks.notifications.background", "Só quando o Apple Music não está na frente"},
    {"tweaks.notifications.never", "Nunca"},
    {"tweaks.player", "Player em tela cheia"},
    {"tweaks.playerHelp", "Onde o player abre. Esc ou o botão de fechar traz o Apple Music de volta."},
    {"tweaks.player.window", "Sobre o Apple Music, numa segunda janela"},
    {"tweaks.player.replace", "Na janela do Apple Music (uma janela só)"},
    {"tweaks.player.workArea", "Tela inteira, com a barra de tarefas visível"},
    {"tweaks.player.screen", "Tela inteira, sem a barra de tarefas (como a Apple)"},
    {"tweaks.playerControls", "Aleatório e repetir no player"},
    {"tweaks.playerControlsHelp", "Adiciona os botões aleatório e repetir do Apple Music ao lado dos do player em tela cheia."},
    {"tweaks.playerControls.shown", "Visíveis"},
    {"tweaks.playerControls.hidden", "Ocultos"},
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
    {"mods.extensions", "Extensões"},
    {"mods.extensionsHelp", "Ative ou desative cada uma. O Marketplace tem outras."},
    {"mods.none", "Ainda não há nenhuma extensão instalada. O Marketplace oferece algumas."},
    {"sound.title", "Som"},
    {"sound.leveler", "Igualar o volume das músicas Dolby Atmos"},
    {"sound.levelerHelp", "Uma música Dolby Atmos costuma tocar mais baixo que as outras. O Ampify a sobe até o nível das músicas estéreo, que ficam como estão."},
    {"mods.look", "Aparência"},
    {"mods.help", "Ajuda"},
    {"mods.folder", "Pasta do Ampify"},
    {"mods.folderHelp", "Onde ficam temas, extensões, ajustes e registros."},
    {"accent.title", "Cor de destaque"},
    {"accent.help", "A cor de botões, links e seleções. Sobre qualquer tema ou sobre a aparência do Apple Music."},
    {"accent.theme", "A do tema"},
    {"accent.custom", "Personalizada…"},
    {"accent.pink", "Rosa Apple Music"},
    {"accent.green", "Verde"},
    {"accent.blue", "Azul"},
    {"accent.purple", "Roxo"},
    {"accent.orange", "Laranja"},
    {"update.title", "Atualizações do Ampify"},
    {"update.help", "As novas versões do Ampify, procuradas uma vez por dia com o Apple Music fechado."},
    {"update.auto", "Instalar sozinhas"},
    {"update.ask", "Perguntar antes"},
    {"update.off", "Nunca"},
    {"update.askTitle", "Ampify {version} está pronta"},
    {"update.askText", "Há uma nova versão do Ampify (você tem a {installed}). Instalar agora? Leva alguns segundos, com o Apple Music fechado."},
    {"update.doneTitle", "Ampify atualizado"},
    {"update.doneText", "Ampify {version} está instalado. Ele carrega na próxima vez que o Apple Music abrir."},
};

constexpr Message kKitRu[] = {
    {"tray.previous", "Предыдущий трек"},
    {"tray.next", "Следующий трек"},
    {"tray.openKit", "Открыть папку Ampify"},
    {"tray.show", "Показать Apple Music"},
    {"tray.quit", "Выйти из Apple Music"},
    {"ui.on", "Вкл."},
    {"ui.off", "Выкл."},
    {"ui.close", "Закрыть"},
    {"page.back", "Назад"},
    {"mods.subtitle", "Темы, расширения и то, что Ampify меняет в Apple Music. Изменения применяются сразу."},
    {"mods.openFolder", "Открыть папку Ampify"},
    {"mods.openMarketplace", "Открыть Marketplace"},
    {"mods.toggleHelp", "Сразу включает или выключает это расширение"},
    {"mods.themeTitle", "Тема: {name}"},
    {"mods.themeSubtitle", "Настройки текущей темы. Изменения видны сразу."},
    {"mods.snippetTitle", "Сниппет: {name}"},
    {"mods.snippetSubtitle", "Настройки включённого сниппета. Изменения видны сразу."},
    {"mods.theme", "Тема"},
    {"mods.themeHelp", "Внешний вид Apple Music. Другие темы есть в Marketplace."},
    {"mods.themeNone", "Нет (стандартный вид Apple Music)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "Что Ampify меняет в поведении Apple Music. Остальное — в настройках Apple."},
    {"tweaks.notifications", "Уведомления о треках"},
    {"tweaks.notificationsHelp", "Apple Music показывает уведомление о каждом новом треке. Отключить их можно в настройках Apple (Основные); Ampify может скрывать их, пока Apple Music на переднем плане."},
    {"tweaks.notifications.apple", "Всегда, как в Apple Music"},
    {"tweaks.notifications.background", "Только когда Apple Music не на переднем плане"},
    {"tweaks.notifications.never", "Никогда"},
    {"tweaks.player", "Полноэкранный плеер"},
    {"tweaks.playerHelp", "Где открывается плеер. Esc или кнопка закрытия возвращает Apple Music."},
    {"tweaks.player.window", "Поверх Apple Music, во втором окне"},
    {"tweaks.player.replace", "В окне Apple Music (одно окно)"},
    {"tweaks.player.workArea", "Весь экран, панель задач видна"},
    {"tweaks.player.screen", "Весь экран, панель задач скрыта (как у Apple)"},
    {"tweaks.playerControls", "Перемешивание и повтор в плеере"},
    {"tweaks.playerControlsHelp", "Добавляет кнопки перемешивания и повтора Apple Music рядом с кнопками полноэкранного плеера."},
    {"tweaks.playerControls.shown", "Показаны"},
    {"tweaks.playerControls.hidden", "Скрыты"},
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
    {"mods.extensions", "Расширения"},
    {"mods.extensionsHelp", "Включайте и выключайте каждое. Другие есть в Marketplace."},
    {"mods.none", "Расширения пока не установлены. Их можно найти в Marketplace."},
    {"sound.title", "Звук"},
    {"sound.leveler", "Выравнивать громкость треков Dolby Atmos"},
    {"sound.levelerHelp", "Трек в Dolby Atmos часто звучит тише остальных. Ampify поднимает его до уровня стереотреков, а стереотреки не меняются."},
    {"mods.look", "Оформление"},
    {"mods.help", "Помощь"},
    {"mods.folder", "Папка Ampify"},
    {"mods.folderHelp", "Здесь хранятся темы, расширения, настройки и журналы."},
    {"accent.title", "Цвет акцента"},
    {"accent.help", "Цвет кнопок, ссылок и выделения. Поверх любой темы или обычного вида Apple Music."},
    {"accent.theme", "Как в теме"},
    {"accent.custom", "Свой…"},
    {"accent.pink", "Розовый Apple Music"},
    {"accent.green", "Зелёный"},
    {"accent.blue", "Синий"},
    {"accent.purple", "Фиолетовый"},
    {"accent.orange", "Оранжевый"},
    {"update.title", "Обновления Ampify"},
    {"update.help", "Новые версии Ampify проверяются раз в день, пока Apple Music закрыт."},
    {"update.auto", "Устанавливать сами"},
    {"update.ask", "Сначала спрашивать"},
    {"update.off", "Никогда"},
    {"update.askTitle", "Ampify {version} готов"},
    {"update.askText", "Доступна новая версия Ampify (у вас {installed}). Установить сейчас? Это займёт несколько секунд, пока Apple Music закрыт."},
    {"update.doneTitle", "Ampify обновлён"},
    {"update.doneText", "Ampify {version} установлен. Он загрузится при следующем запуске Apple Music."},
};

constexpr Message kKitJa[] = {
    {"tray.previous", "前の曲"},
    {"tray.next", "次の曲"},
    {"tray.openKit", "Ampify フォルダーを開く"},
    {"tray.show", "Apple Music を表示"},
    {"tray.quit", "Apple Music を終了"},
    {"ui.on", "オン"},
    {"ui.off", "オフ"},
    {"ui.close", "閉じる"},
    {"page.back", "戻る"},
    {"mods.subtitle", "テーマ、拡張機能、そして Ampify が Apple Music で変えることです。変更はすぐに反映されます。"},
    {"mods.openFolder", "Ampify フォルダーを開く"},
    {"mods.openMarketplace", "Marketplace を開く"},
    {"mods.toggleHelp", "この拡張機能をすぐにオン・オフします"},
    {"mods.themeTitle", "テーマ: {name}"},
    {"mods.themeSubtitle", "使用中のテーマの設定です。変更はすぐに表示されます。"},
    {"mods.snippetTitle", "スニペット: {name}"},
    {"mods.snippetSubtitle", "オンにしたスニペットの設定です。変更はすぐに表示されます。"},
    {"mods.theme", "テーマ"},
    {"mods.themeHelp", "Apple Music の見た目です。Marketplace にほかのテーマもあります。"},
    {"mods.themeNone", "なし (Apple Music 標準の見た目)"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "Ampify が Apple Music の動作で変えることです。そのほかは Apple の設定にあります。"},
    {"tweaks.notifications", "曲の通知"},
    {"tweaks.notificationsHelp", "Apple Music は新しい曲ごとに通知を表示します。オフにするのは Apple の設定 (一般) です。Ampify は Apple Music が前面にある間、通知を表示しないようにできます。"},
    {"tweaks.notifications.apple", "常に表示 (Apple Music と同じ)"},
    {"tweaks.notifications.background", "Apple Music が前面にないときだけ"},
    {"tweaks.notifications.never", "表示しない"},
    {"tweaks.player", "フルスクリーンプレーヤー"},
    {"tweaks.playerHelp", "プレーヤーが開く場所です。Esc キーか閉じるボタンで Apple Music に戻ります。"},
    {"tweaks.player.window", "Apple Music の上に、別のウインドウで"},
    {"tweaks.player.replace", "Apple Music のウインドウの中に (ウインドウは 1 つ)"},
    {"tweaks.player.workArea", "画面全体 (タスクバーは表示)"},
    {"tweaks.player.screen", "画面全体 (タスクバーも隠す、Apple と同じ)"},
    {"tweaks.playerControls", "プレーヤーのシャッフルとリピート"},
    {"tweaks.playerControlsHelp", "フルスクリーンプレーヤーのボタンの両側に、Apple Music のシャッフルとリピートを加えます。"},
    {"tweaks.playerControls.shown", "表示"},
    {"tweaks.playerControls.hidden", "非表示"},
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
    {"mods.extensions", "拡張機能"},
    {"mods.extensionsHelp", "それぞれオンまたはオフにできます。ほかの拡張機能は Marketplace にあります。"},
    {"mods.none", "まだ拡張機能はインストールされていません。Marketplace で見つけられます。"},
    {"sound.title", "サウンド"},
    {"sound.leveler", "Dolby Atmos の曲の音量をそろえる"},
    {"sound.levelerHelp", "Dolby Atmos の曲はほかの曲より小さく聞こえることがよくあります。Ampify がステレオの曲と同じ音量まで上げます。ステレオの曲はそのままです。"},
    {"mods.look", "外観"},
    {"mods.help", "ヘルプ"},
    {"mods.folder", "Ampify フォルダー"},
    {"mods.folderHelp", "テーマ、拡張機能、設定、ログが保存されている場所です。"},
    {"accent.title", "アクセントカラー"},
    {"accent.help", "ボタン、リンク、選択部分の色です。どのテーマにも、Apple Music 本来の見た目にも使えます。"},
    {"accent.theme", "テーマの色"},
    {"accent.custom", "カスタム…"},
    {"accent.pink", "Apple Music のピンク"},
    {"accent.green", "グリーン"},
    {"accent.blue", "ブルー"},
    {"accent.purple", "パープル"},
    {"accent.orange", "オレンジ"},
    {"update.title", "Ampify のアップデート"},
    {"update.help", "Apple Music を閉じている間に、1 日 1 回 Ampify の新しいバージョンを確認します。"},
    {"update.auto", "自動でインストール"},
    {"update.ask", "先に確認する"},
    {"update.off", "しない"},
    {"update.askTitle", "Ampify {version} の準備ができました"},
    {"update.askText", "Ampify の新しいバージョンがあります（現在は {installed}）。今すぐインストールしますか？ Apple Music を閉じている間、数秒で終わります。"},
    {"update.doneTitle", "Ampify を更新しました"},
    {"update.doneText", "Ampify {version} をインストールしました。次に Apple Music を開いたときに読み込まれます。"},
};

constexpr Message kKitZh[] = {
    {"tray.previous", "上一首"},
    {"tray.next", "下一首"},
    {"tray.openKit", "打开 Ampify 文件夹"},
    {"tray.show", "显示 Apple Music"},
    {"tray.quit", "退出 Apple Music"},
    {"ui.on", "开"},
    {"ui.off", "关"},
    {"ui.close", "关闭"},
    {"page.back", "返回"},
    {"mods.subtitle", "主题、扩展，以及 Ampify 对 Apple Music 的改动。更改会立即生效。"},
    {"mods.openFolder", "打开 Ampify 文件夹"},
    {"mods.openMarketplace", "打开 Marketplace"},
    {"mods.toggleHelp", "立即打开或关闭此扩展"},
    {"mods.themeTitle", "主题：{name}"},
    {"mods.themeSubtitle", "当前主题的设置。更改会立即显示。"},
    {"mods.snippetTitle", "代码片段：{name}"},
    {"mods.snippetSubtitle", "已启用的代码片段的设置。更改会立即显示。"},
    {"mods.theme", "主题"},
    {"mods.themeHelp", "Apple Music 的外观。Marketplace 里还有更多主题。"},
    {"mods.themeNone", "无（Apple Music 默认外观）"},
    {"tweaks.title", "Apple Music"},
    {"tweaks.subtitle", "Ampify 对 Apple Music 行为的改动。其他设置在 Apple 自己的设置中。"},
    {"tweaks.notifications", "歌曲通知"},
    {"tweaks.notificationsHelp", "Apple Music 每播放一首新歌都会显示通知。关闭通知请用 Apple 的设置（通用）；Ampify 可以在 Apple Music 位于前台时不显示通知。"},
    {"tweaks.notifications.apple", "始终显示（与 Apple Music 相同）"},
    {"tweaks.notifications.background", "仅当 Apple Music 不在前台时"},
    {"tweaks.notifications.never", "从不"},
    {"tweaks.player", "全屏播放器"},
    {"tweaks.playerHelp", "播放器打开的位置。按 Esc 或关闭按钮即可回到 Apple Music。"},
    {"tweaks.player.window", "在 Apple Music 上方，作为第二个窗口"},
    {"tweaks.player.replace", "在 Apple Music 窗口中（只有一个窗口）"},
    {"tweaks.player.workArea", "整个屏幕，显示任务栏"},
    {"tweaks.player.screen", "整个屏幕，隐藏任务栏（与 Apple 相同）"},
    {"tweaks.playerControls", "播放器中的随机播放和重复"},
    {"tweaks.playerControlsHelp", "在全屏播放器的按钮两侧加上 Apple Music 的随机播放和重复按钮。"},
    {"tweaks.playerControls.shown", "显示"},
    {"tweaks.playerControls.hidden", "隐藏"},
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
    {"mods.extensions", "扩展"},
    {"mods.extensionsHelp", "可以逐个开启或关闭。Marketplace 中还有更多。"},
    {"mods.none", "还没有安装任何扩展。可以在 Marketplace 中找到。"},
    {"sound.title", "声音"},
    {"sound.leveler", "平衡杜比全景声歌曲的音量"},
    {"sound.levelerHelp", "杜比全景声歌曲听起来常常比其他歌曲小声。Ampify 会把它提高到立体声歌曲的音量，立体声歌曲保持不变。"},
    {"mods.look", "外观"},
    {"mods.help", "帮助"},
    {"mods.folder", "Ampify 文件夹"},
    {"mods.folderHelp", "存放主题、扩展、设置和日志的位置。"},
    {"accent.title", "强调色"},
    {"accent.help", "按钮、链接和选中项的颜色。可用于任何主题，也可用于 Apple Music 原本的外观。"},
    {"accent.theme", "使用主题的颜色"},
    {"accent.custom", "自定义…"},
    {"accent.pink", "Apple Music 粉色"},
    {"accent.green", "绿色"},
    {"accent.blue", "蓝色"},
    {"accent.purple", "紫色"},
    {"accent.orange", "橙色"},
    {"update.title", "Ampify 更新"},
    {"update.help", "在 Apple Music 关闭时，每天检查一次 Ampify 的新版本。"},
    {"update.auto", "自动安装"},
    {"update.ask", "先问我"},
    {"update.off", "从不"},
    {"update.askTitle", "Ampify {version} 已就绪"},
    {"update.askText", "Ampify 有新版本（当前为 {installed}）。现在安装吗？Apple Music 关闭时，只需几秒钟。"},
    {"update.doneTitle", "Ampify 已更新"},
    {"update.doneText", "Ampify {version} 已安装，下次打开 Apple Music 时加载。"},
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

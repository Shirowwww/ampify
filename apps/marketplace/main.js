// The Marketplace: a page of Apple Music to install, remove and choose themes, schemes, extensions and apps. It does
// nothing itself: every action goes through ampify (Ampify.kit.run), with the same checks as in the console (SHA-256,
// verified archive, backup before a removal). Specification: docs/extensions/catalog.md.
const { h, useState, useEffect } = Ampify.React;
const { Card, Button, ButtonPrimary, ButtonTertiary, Dropdown, Slider, Text, TextField, Toggle } = Ampify.components;

// Texts in English, French, German, Spanish, Portuguese, Russian, Japanese and Chinese; Ampify.i18n picks the Windows
// display language. Apple Music's own words where it has one (Activé, Désactivé, Retour, Einstellungen).
Ampify.i18n.define({
  en: {
    "tab.theme": "Themes", "tab.extension": "Extensions", "tab.app": "Apps", "tab.snippet": "Snippets", "tab.installed": "Installed",
    "empty.theme": "No theme in the catalog yet.",
    "empty.extension": "No extension in the catalog yet.",
    "empty.app": "No app in the catalog yet.",
    "empty.snippet": "No snippet in the catalog yet. A snippet is a few lines of user.css: ampify new snippet starts one.",
    "empty.installed": "Nothing installed yet. Pick a theme or an extension in the other tabs.",
    "kind.theme": "Theme", "kind.palette": "Colours", "kind.extension": "Extension", "kind.app": "App", "kind.snippet": "Snippet",
    "by": "by {author}", "version": "version {version}",
    "state.broken": "Cannot load, see ampify doctor", "state.installed": "Installed", "state.inUse": "In use",
    "state.on": "On", "state.off": "Off", "state.update": "Update to {version}",
    "action.install": "Install", "action.update": "Update", "action.use": "Use", "action.turnOff": "Turn off",
    "action.turnOn": "Turn on", "action.details": "Details", "action.uninstall": "Uninstall",
    "action.confirmUninstall": "Confirm uninstall", "action.refresh": "Refresh", "action.clearSearch": "Clear the search",
    "action.appleLook": "Use Apple Music's look", "action.previous": "Previous", "action.next": "Next",
    "uninstall.themeFirst": "Turn the theme off first",
    "perm.none": "Asks for no permission.", "perm.can": "It can:", "perm.one": "Asks for 1 permission", "perm.count": "Asks for {count} permissions",
    "perm.new": "new in version {version}",
    "scheme": "Scheme", "tags": "Tags: {tags}",
    "theme.none": "No theme: Apple Music's own look.", "theme.inUse": "Theme in use: {name}",
    "search.theme": "Search themes", "search.extension": "Search extensions", "search.app": "Search apps",
    "search.snippet": "Search snippets", "search.installed": "Search what is installed",
    "search.none": "Nothing matches \"{search}\"",
    "search.where": "Search looks in names, authors, descriptions and tags.",
    "catalog.unreadable": "The catalog cannot be read",
    "catalog.unreadableText": "Check the connection, then refresh. What is installed stays under Installed.",
    "catalog.offline": "The catalog cannot be read right now: these are the installed copies.",
    "empty.title": "Nothing here yet", "page": "Page {page} of {pages}",
    "status.reading": "Reading the catalog…", "status.working": "Working…",
    "status.unreadable": "Cannot read the state of Ampify: {error}", "status.unreadableReply": "unreadable reply",
    "status.failed": "Failed: {error}", "status.failedCode": "Failed (code {code})",
    "done.installedInUse": "{name} installed and in use", "done.installedOn": "{name} installed and on",
    "done.installedUse": "{name} installed. Read what it can do, then use it.",
    "done.installedTurnOn": "{name} installed. Read what it can do, then turn it on.",
    "done.updated": "{name} updated to {version}", "done.inUse": "{name} in use",
    "done.inUseScheme": "{name} in use, {scheme} scheme", "done.appleLook": "Apple Music's own look is back",
    "done.on": "{name} turned on", "done.off": "{name} turned off", "done.removed": "{name} uninstalled",
    "preview": "Preview of {name}",
    "detail.settings": "Settings", "detail.settingsNote": "Changes apply at once. You also find them in Apple Music's Settings, under Ampify.", "done.setting": "{name}: {setting} changed", "action.settingsOf": "Settings of {name}",
    "detail.readmeLoading": "Reading its description…",
    "action.settings": "Settings", "action.openSettings": "Open Ampify's settings", "detail.can": "What it can do", "detail.about": "About", "detail.kind": "Type", "detail.author": "Made by", "detail.version": "Version", "detail.tags": "Tags", "detail.gone": "This element is no longer in the catalog, nor installed.", "detail.noDescription": "No description yet.",
  },
  fr: {
    "tab.theme": "Thèmes", "tab.extension": "Extensions", "tab.app": "Apps", "tab.snippet": "Snippets", "tab.installed": "Installés",
    "empty.theme": "Aucun thème dans le catalogue pour l'instant.",
    "empty.extension": "Aucune extension dans le catalogue pour l'instant.",
    "empty.app": "Aucune app dans le catalogue pour l'instant.",
    "empty.snippet": "Aucun snippet dans le catalogue pour l'instant. Un snippet tient en quelques lignes de user.css : ampify new snippet en crée un.",
    "empty.installed": "Rien d'installé pour l'instant. Choisissez un thème ou une extension dans les autres onglets.",
    "kind.theme": "Thème", "kind.palette": "Couleurs", "kind.extension": "Extension", "kind.app": "App", "kind.snippet": "Snippet",
    "by": "par {author}", "version": "version {version}",
    "state.broken": "Ne se charge pas, voir ampify doctor", "state.installed": "Installé", "state.inUse": "Utilisé",
    "state.on": "Activé", "state.off": "Désactivé", "state.update": "Mise à jour {version}",
    "action.install": "Installer", "action.update": "Mettre à jour", "action.use": "Utiliser", "action.turnOff": "Désactiver",
    "action.turnOn": "Activer", "action.details": "Détails", "action.uninstall": "Désinstaller",
    "action.confirmUninstall": "Confirmer la désinstallation", "action.refresh": "Actualiser",
    "action.clearSearch": "Effacer la recherche", "action.appleLook": "Revenir au look d'Apple Music",
    "action.previous": "Précédente", "action.next": "Suivante",
    "uninstall.themeFirst": "Désactivez d'abord le thème",
    "perm.none": "Ne demande aucune permission.", "perm.can": "Il peut :", "perm.one": "Demande 1 permission", "perm.count": "Demande {count} permissions",
    "perm.new": "nouveau dans la version {version}",
    "scheme": "Variante", "tags": "Mots-clés : {tags}",
    "theme.none": "Aucun thème : le look d'Apple Music.", "theme.inUse": "Thème utilisé : {name}",
    "search.theme": "Rechercher un thème", "search.extension": "Rechercher une extension", "search.app": "Rechercher une app",
    "search.snippet": "Rechercher un snippet", "search.installed": "Rechercher dans ce qui est installé",
    "search.none": "Rien ne correspond à « {search} »",
    "search.where": "La recherche porte sur les noms, auteurs, descriptions et mots-clés.",
    "catalog.unreadable": "Le catalogue est illisible",
    "catalog.unreadableText": "Vérifiez la connexion, puis actualisez. Ce qui est installé reste dans Installés.",
    "catalog.offline": "Le catalogue est illisible pour l'instant : voici les copies installées.",
    "empty.title": "Rien ici pour l'instant", "page": "Page {page} sur {pages}",
    "status.reading": "Lecture du catalogue…", "status.working": "En cours…",
    "status.unreadable": "Impossible de lire l'état d'Ampify : {error}", "status.unreadableReply": "réponse illisible",
    "status.failed": "Échec : {error}", "status.failedCode": "Échec (code {code})",
    "done.installedInUse": "{name} installé et utilisé", "done.installedOn": "{name} installé et activé",
    "done.installedUse": "{name} installé. Lisez ce qu'il peut faire, puis utilisez-le.",
    "done.installedTurnOn": "{name} installé. Lisez ce qu'il peut faire, puis activez-le.",
    "done.updated": "{name} mis à jour en {version}", "done.inUse": "{name} utilisé",
    "done.inUseScheme": "{name} utilisé, variante {scheme}", "done.appleLook": "Le look d'Apple Music est revenu",
    "done.on": "{name} activé", "done.off": "{name} désactivé", "done.removed": "{name} désinstallé",
    "preview": "Aperçu de {name}",
    "detail.settings": "Réglages", "detail.settingsNote": "Les changements s'appliquent tout de suite. Ils sont aussi dans les Réglages d'Apple Music, sous Ampify.", "done.setting": "{name} : {setting} modifié", "action.settingsOf": "Réglages de {name}",
    "detail.readmeLoading": "Lecture de sa présentation…",
    "action.settings": "Réglages", "action.openSettings": "Ouvrir les réglages d'Ampify", "detail.can": "Ce qu'il peut faire", "detail.about": "À propos", "detail.kind": "Type", "detail.author": "Créé par", "detail.version": "Version", "detail.tags": "Mots-clés", "detail.gone": "Cet élément n'est plus dans le catalogue ni installé.", "detail.noDescription": "Pas encore de description.",
  },
  de: {
    "tab.theme": "Designs", "tab.extension": "Erweiterungen", "tab.app": "Apps", "tab.snippet": "Snippets", "tab.installed": "Installiert",
    "empty.theme": "Noch kein Design im Katalog.",
    "empty.extension": "Noch keine Erweiterung im Katalog.",
    "empty.app": "Noch keine App im Katalog.",
    "empty.snippet": "Noch kein Snippet im Katalog. Ein Snippet besteht aus ein paar Zeilen user.css: ampify new snippet legt eines an.",
    "empty.installed": "Noch nichts installiert. Wählen Sie in den anderen Tabs ein Design oder eine Erweiterung aus.",
    "kind.theme": "Design", "kind.palette": "Farben", "kind.extension": "Erweiterung", "kind.app": "App", "kind.snippet": "Snippet",
    "by": "von {author}", "version": "Version {version}",
    "state.broken": "Lässt sich nicht laden, siehe ampify doctor", "state.installed": "Installiert", "state.inUse": "In Benutzung",
    "state.on": "Aktiviert", "state.off": "Deaktiviert", "state.update": "Update auf {version}",
    "action.install": "Installieren", "action.update": "Aktualisieren", "action.use": "Verwenden", "action.turnOff": "Deaktivieren",
    "action.turnOn": "Aktivieren", "action.details": "Details", "action.uninstall": "Deinstallieren",
    "action.confirmUninstall": "Deinstallation bestätigen", "action.refresh": "Aktualisieren",
    "action.clearSearch": "Suche löschen", "action.appleLook": "Look von Apple Music verwenden",
    "action.previous": "Zurück", "action.next": "Weiter",
    "uninstall.themeFirst": "Deaktivieren Sie zuerst das Design",
    "perm.none": "Benötigt keine Berechtigung.", "perm.can": "Es kann:", "perm.one": "Benötigt 1 Berechtigung", "perm.count": "Benötigt {count} Berechtigungen",
    "perm.new": "neu in Version {version}",
    "scheme": "Variante", "tags": "Stichwörter: {tags}",
    "theme.none": "Kein Design: der eigene Look von Apple Music.", "theme.inUse": "Aktives Design: {name}",
    "search.theme": "Designs suchen", "search.extension": "Erweiterungen suchen", "search.app": "Apps suchen",
    "search.snippet": "Snippets suchen", "search.installed": "In den installierten suchen",
    "search.none": "Nichts passt zu „{search}“",
    "search.where": "Die Suche berücksichtigt Namen, Autoren, Beschreibungen und Stichwörter.",
    "catalog.unreadable": "Der Katalog kann nicht gelesen werden",
    "catalog.unreadableText": "Prüfen Sie die Verbindung und aktualisieren Sie dann. Was installiert ist, bleibt unter Installiert.",
    "catalog.offline": "Der Katalog kann gerade nicht gelesen werden: Das sind die installierten Kopien.",
    "empty.title": "Hier ist noch nichts", "page": "Seite {page} von {pages}",
    "status.reading": "Katalog wird gelesen …", "status.working": "Wird ausgeführt …",
    "status.unreadable": "Der Zustand von Ampify kann nicht gelesen werden: {error}", "status.unreadableReply": "unlesbare Antwort",
    "status.failed": "Fehlgeschlagen: {error}", "status.failedCode": "Fehlgeschlagen (Code {code})",
    "done.installedInUse": "{name} installiert und in Benutzung", "done.installedOn": "{name} installiert und aktiviert",
    "done.installedUse": "{name} installiert. Lesen Sie, was es kann, und verwenden Sie es dann.",
    "done.installedTurnOn": "{name} installiert. Lesen Sie, was es kann, und aktivieren Sie es dann.",
    "done.updated": "{name} auf {version} aktualisiert", "done.inUse": "{name} in Benutzung",
    "done.inUseScheme": "{name} in Benutzung, Variante {scheme}", "done.appleLook": "Der eigene Look von Apple Music ist zurück",
    "done.on": "{name} aktiviert", "done.off": "{name} deaktiviert", "done.removed": "{name} deinstalliert",
    "preview": "Vorschau von {name}",
    "detail.settings": "Einstellungen", "detail.settingsNote": "Änderungen gelten sofort. Sie finden sie auch in den Einstellungen von Apple Music unter Ampify.", "done.setting": "{name}: {setting} geändert", "action.settingsOf": "Einstellungen von {name}",
    "detail.readmeLoading": "Beschreibung wird gelesen…",
    "action.settings": "Einstellungen", "action.openSettings": "Ampify-Einstellungen öffnen", "detail.can": "Was es kann", "detail.about": "Info", "detail.kind": "Typ", "detail.author": "Erstellt von", "detail.version": "Version", "detail.tags": "Stichwörter", "detail.gone": "Dieses Element ist weder im Katalog noch installiert.", "detail.noDescription": "Noch keine Beschreibung.",
  },
  es: {
    "tab.theme": "Temas", "tab.extension": "Extensiones", "tab.app": "Apps", "tab.snippet": "Snippets", "tab.installed": "Instalados",
    "empty.theme": "Todavía no hay ningún tema en el catálogo.",
    "empty.extension": "Todavía no hay ninguna extensión en el catálogo.",
    "empty.app": "Todavía no hay ninguna app en el catálogo.",
    "empty.snippet": "Todavía no hay ningún snippet en el catálogo. Un snippet son unas pocas líneas de user.css: ampify new snippet crea uno.",
    "empty.installed": "Todavía no hay nada instalado. Elige un tema o una extensión en las otras pestañas.",
    "kind.theme": "Tema", "kind.palette": "Colores", "kind.extension": "Extensión", "kind.app": "App", "kind.snippet": "Snippet",
    "by": "de {author}", "version": "versión {version}",
    "state.broken": "No se puede cargar, mira ampify doctor", "state.installed": "Instalado", "state.inUse": "En uso",
    "state.on": "Activado", "state.off": "Desactivado", "state.update": "Actualizar a {version}",
    "action.install": "Instalar", "action.update": "Actualizar", "action.use": "Usar", "action.turnOff": "Desactivar",
    "action.turnOn": "Activar", "action.details": "Detalles", "action.uninstall": "Desinstalar",
    "action.confirmUninstall": "Confirmar la desinstalación", "action.refresh": "Actualizar",
    "action.clearSearch": "Borrar la búsqueda", "action.appleLook": "Usar el aspecto de Apple Music",
    "action.previous": "Anterior", "action.next": "Siguiente",
    "uninstall.themeFirst": "Desactiva primero el tema",
    "perm.none": "No pide ningún permiso.", "perm.can": "Puede:", "perm.one": "Pide 1 permiso", "perm.count": "Pide {count} permisos",
    "perm.new": "nuevo en la versión {version}",
    "scheme": "Variante", "tags": "Etiquetas: {tags}",
    "theme.none": "Ningún tema: el aspecto propio de Apple Music.", "theme.inUse": "Tema en uso: {name}",
    "search.theme": "Buscar temas", "search.extension": "Buscar extensiones", "search.app": "Buscar apps",
    "search.snippet": "Buscar snippets", "search.installed": "Buscar entre lo instalado",
    "search.none": "Nada coincide con «{search}»",
    "search.where": "La búsqueda mira los nombres, autores, descripciones y etiquetas.",
    "catalog.unreadable": "No se puede leer el catálogo",
    "catalog.unreadableText": "Comprueba la conexión y vuelve a actualizar. Lo que está instalado sigue en Instalados.",
    "catalog.offline": "Ahora mismo no se puede leer el catálogo: estas son las copias instaladas.",
    "empty.title": "Aquí todavía no hay nada", "page": "Página {page} de {pages}",
    "status.reading": "Leyendo el catálogo…", "status.working": "Trabajando…",
    "status.unreadable": "No se puede leer el estado de Ampify: {error}", "status.unreadableReply": "respuesta ilegible",
    "status.failed": "Error: {error}", "status.failedCode": "Error (código {code})",
    "done.installedInUse": "{name} instalado y en uso", "done.installedOn": "{name} instalado y activado",
    "done.installedUse": "{name} instalado. Lee lo que puede hacer y luego úsalo.",
    "done.installedTurnOn": "{name} instalado. Lee lo que puede hacer y luego actívalo.",
    "done.updated": "{name} actualizado a {version}", "done.inUse": "{name} en uso",
    "done.inUseScheme": "{name} en uso, variante {scheme}", "done.appleLook": "Ha vuelto el aspecto propio de Apple Music",
    "done.on": "{name} activado", "done.off": "{name} desactivado", "done.removed": "{name} desinstalado",
    "preview": "Vista previa de {name}",
    "detail.settings": "Ajustes", "detail.settingsNote": "Los cambios se aplican al instante. También están en los Ajustes de Apple Music, en Ampify.", "done.setting": "{name}: {setting} cambiado", "action.settingsOf": "Ajustes de {name}",
    "detail.readmeLoading": "Leyendo su presentación…",
    "action.settings": "Ajustes", "action.openSettings": "Abrir los ajustes de Ampify", "detail.can": "Lo que puede hacer", "detail.about": "Información", "detail.kind": "Tipo", "detail.author": "Creado por", "detail.version": "Versión", "detail.tags": "Etiquetas", "detail.gone": "Este elemento ya no está en el catálogo ni instalado.", "detail.noDescription": "Todavía no hay descripción.",
  },
  pt: {
    "tab.theme": "Temas", "tab.extension": "Extensões", "tab.app": "Apps", "tab.snippet": "Snippets", "tab.installed": "Instalados",
    "empty.theme": "Ainda não há nenhum tema no catálogo.",
    "empty.extension": "Ainda não há nenhuma extensão no catálogo.",
    "empty.app": "Ainda não há nenhum app no catálogo.",
    "empty.snippet": "Ainda não há nenhum snippet no catálogo. Um snippet são poucas linhas de user.css: ampify new snippet cria um.",
    "empty.installed": "Ainda não há nada instalado. Escolha um tema ou uma extensão nas outras abas.",
    "kind.theme": "Tema", "kind.palette": "Cores", "kind.extension": "Extensão", "kind.app": "App", "kind.snippet": "Snippet",
    "by": "por {author}", "version": "versão {version}",
    "state.broken": "Não carrega, veja ampify doctor", "state.installed": "Instalado", "state.inUse": "Em uso",
    "state.on": "Ativado", "state.off": "Desativado", "state.update": "Atualizar para {version}",
    "action.install": "Instalar", "action.update": "Atualizar", "action.use": "Usar", "action.turnOff": "Desativar",
    "action.turnOn": "Ativar", "action.details": "Detalhes", "action.uninstall": "Desinstalar",
    "action.confirmUninstall": "Confirmar a desinstalação", "action.refresh": "Atualizar",
    "action.clearSearch": "Limpar a busca", "action.appleLook": "Usar o visual do Apple Music",
    "action.previous": "Anterior", "action.next": "Próxima",
    "uninstall.themeFirst": "Desative o tema primeiro",
    "perm.none": "Não pede nenhuma permissão.", "perm.can": "Ele pode:", "perm.one": "Pede 1 permissão", "perm.count": "Pede {count} permissões",
    "perm.new": "novo na versão {version}",
    "scheme": "Variante", "tags": "Etiquetas: {tags}",
    "theme.none": "Nenhum tema: o visual próprio do Apple Music.", "theme.inUse": "Tema em uso: {name}",
    "search.theme": "Buscar temas", "search.extension": "Buscar extensões", "search.app": "Buscar apps",
    "search.snippet": "Buscar snippets", "search.installed": "Buscar entre os instalados",
    "search.none": "Nada corresponde a “{search}”",
    "search.where": "A busca olha nomes, autores, descrições e etiquetas.",
    "catalog.unreadable": "Não foi possível ler o catálogo",
    "catalog.unreadableText": "Verifique a conexão e atualize. O que está instalado continua em Instalados.",
    "catalog.offline": "Não foi possível ler o catálogo agora: estas são as cópias instaladas.",
    "empty.title": "Ainda não há nada aqui", "page": "Página {page} de {pages}",
    "status.reading": "Lendo o catálogo…", "status.working": "Trabalhando…",
    "status.unreadable": "Não foi possível ler o estado do Ampify: {error}", "status.unreadableReply": "resposta ilegível",
    "status.failed": "Falhou: {error}", "status.failedCode": "Falhou (código {code})",
    "done.installedInUse": "{name} instalado e em uso", "done.installedOn": "{name} instalado e ativado",
    "done.installedUse": "{name} instalado. Leia o que ele pode fazer e depois use-o.",
    "done.installedTurnOn": "{name} instalado. Leia o que ele pode fazer e depois ative-o.",
    "done.updated": "{name} atualizado para {version}", "done.inUse": "{name} em uso",
    "done.inUseScheme": "{name} em uso, variante {scheme}", "done.appleLook": "O visual próprio do Apple Music voltou",
    "done.on": "{name} ativado", "done.off": "{name} desativado", "done.removed": "{name} desinstalado",
    "preview": "Prévia de {name}",
    "detail.settings": "Ajustes", "detail.settingsNote": "As mudanças valem na hora. Também estão nos Ajustes do Apple Music, em Ampify.", "done.setting": "{name}: {setting} alterado", "action.settingsOf": "Ajustes de {name}",
    "detail.readmeLoading": "Lendo a apresentação…",
    "action.settings": "Ajustes", "action.openSettings": "Abrir os ajustes do Ampify", "detail.can": "O que ele pode fazer", "detail.about": "Sobre", "detail.kind": "Tipo", "detail.author": "Criado por", "detail.version": "Versão", "detail.tags": "Etiquetas", "detail.gone": "Este elemento não está mais no catálogo nem instalado.", "detail.noDescription": "Ainda não há descrição.",
  },
  ru: {
    "tab.theme": "Темы", "tab.extension": "Расширения", "tab.app": "Приложения", "tab.snippet": "Сниппеты", "tab.installed": "Установлено",
    "empty.theme": "В каталоге пока нет тем.",
    "empty.extension": "В каталоге пока нет расширений.",
    "empty.app": "В каталоге пока нет приложений.",
    "empty.snippet": "В каталоге пока нет сниппетов. Сниппет — это несколько строк user.css: ampify new snippet создаёт его.",
    "empty.installed": "Пока ничего не установлено. Выберите тему или расширение на других вкладках.",
    "kind.theme": "Тема", "kind.palette": "Цвета", "kind.extension": "Расширение", "kind.app": "Приложение", "kind.snippet": "Сниппет",
    "by": "автор: {author}", "version": "версия {version}",
    "state.broken": "Не загружается, см. ampify doctor", "state.installed": "Установлено", "state.inUse": "Используется",
    "state.on": "Включено", "state.off": "Выключено", "state.update": "Обновить до {version}",
    "action.install": "Установить", "action.update": "Обновить", "action.use": "Использовать", "action.turnOff": "Выключить",
    "action.turnOn": "Включить", "action.details": "Подробности", "action.uninstall": "Удалить",
    "action.confirmUninstall": "Подтвердить удаление", "action.refresh": "Обновить",
    "action.clearSearch": "Очистить поиск", "action.appleLook": "Вернуть вид Apple Music",
    "action.previous": "Назад", "action.next": "Вперёд",
    "uninstall.themeFirst": "Сначала выключите тему",
    "perm.none": "Не запрашивает разрешений.", "perm.can": "Может:", "perm.one": "Запрашивает 1 разрешение", "perm.count": "Запрашивает разрешений: {count}",
    "perm.new": "новое в версии {version}",
    "scheme": "Вариант", "tags": "Теги: {tags}",
    "theme.none": "Без темы: собственный вид Apple Music.", "theme.inUse": "Используемая тема: {name}",
    "search.theme": "Поиск тем", "search.extension": "Поиск расширений", "search.app": "Поиск приложений",
    "search.snippet": "Поиск сниппетов", "search.installed": "Поиск среди установленного",
    "search.none": "Ничего не найдено по запросу «{search}»",
    "search.where": "Поиск идёт по названиям, авторам, описаниям и тегам.",
    "catalog.unreadable": "Не удаётся прочитать каталог",
    "catalog.unreadableText": "Проверьте подключение и обновите страницу. Установленное остаётся во вкладке «Установлено».",
    "catalog.offline": "Сейчас не удаётся прочитать каталог: показаны установленные копии.",
    "empty.title": "Здесь пока ничего нет", "page": "Страница {page} из {pages}",
    "status.reading": "Чтение каталога…", "status.working": "Выполняется…",
    "status.unreadable": "Не удаётся прочитать состояние Ampify: {error}", "status.unreadableReply": "нечитаемый ответ",
    "status.failed": "Ошибка: {error}", "status.failedCode": "Ошибка (код {code})",
    "done.installedInUse": "{name} установлено и используется", "done.installedOn": "{name} установлено и включено",
    "done.installedUse": "{name} установлено. Прочитайте, что оно умеет, и начните использовать.",
    "done.installedTurnOn": "{name} установлено. Прочитайте, что оно умеет, и включите его.",
    "done.updated": "{name} обновлено до {version}", "done.inUse": "{name} используется",
    "done.inUseScheme": "{name} используется, вариант {scheme}", "done.appleLook": "Вернулся собственный вид Apple Music",
    "done.on": "{name} включено", "done.off": "{name} выключено", "done.removed": "{name} удалено",
    "preview": "Предпросмотр: {name}",
    "detail.settings": "Настройки", "detail.settingsNote": "Изменения применяются сразу. Они есть и в настройках Apple Music, в разделе Ampify.", "done.setting": "{name}: «{setting}» изменено", "action.settingsOf": "Настройки {name}",
    "detail.readmeLoading": "Чтение описания…",
    "action.settings": "Настройки", "action.openSettings": "Открыть настройки Ampify", "detail.can": "Что оно умеет", "detail.about": "Сведения", "detail.kind": "Тип", "detail.author": "Автор", "detail.version": "Версия", "detail.tags": "Теги", "detail.gone": "Этого элемента больше нет ни в каталоге, ни среди установленных.", "detail.noDescription": "Описания пока нет.",
  },
  ja: {
    "tab.theme": "テーマ", "tab.extension": "拡張機能", "tab.app": "アプリ", "tab.snippet": "スニペット", "tab.installed": "インストール済み",
    "empty.theme": "カタログにはまだテーマがありません。",
    "empty.extension": "カタログにはまだ拡張機能がありません。",
    "empty.app": "カタログにはまだアプリがありません。",
    "empty.snippet": "カタログにはまだスニペットがありません。スニペットは user.css の数行です。ampify new snippet で作れます。",
    "empty.installed": "まだ何もインストールされていません。ほかのタブからテーマや拡張機能を選んでください。",
    "kind.theme": "テーマ", "kind.palette": "カラー", "kind.extension": "拡張機能", "kind.app": "アプリ", "kind.snippet": "スニペット",
    "by": "作者： {author}", "version": "バージョン {version}",
    "state.broken": "読み込めません。ampify doctor を確認してください", "state.installed": "インストール済み", "state.inUse": "使用中",
    "state.on": "オン", "state.off": "オフ", "state.update": "{version} に更新",
    "action.install": "インストール", "action.update": "更新", "action.use": "使う", "action.turnOff": "オフにする",
    "action.turnOn": "オンにする", "action.details": "詳細", "action.uninstall": "アンインストール",
    "action.confirmUninstall": "アンインストールを確認", "action.refresh": "再読み込み",
    "action.clearSearch": "検索をクリア", "action.appleLook": "Apple Music の見た目に戻す",
    "action.previous": "前へ", "action.next": "次へ",
    "uninstall.themeFirst": "先にテーマをオフにしてください",
    "perm.none": "権限は必要ありません。", "perm.can": "できること：", "perm.one": "1 件の権限が必要です", "perm.count": "{count} 件の権限が必要です",
    "perm.new": "バージョン {version} で追加",
    "scheme": "バリエーション", "tags": "タグ： {tags}",
    "theme.none": "テーマなし： Apple Music 本来の見た目です。", "theme.inUse": "使用中のテーマ： {name}",
    "search.theme": "テーマを検索", "search.extension": "拡張機能を検索", "search.app": "アプリを検索",
    "search.snippet": "スニペットを検索", "search.installed": "インストール済みから検索",
    "search.none": "「{search}」に一致するものはありません",
    "search.where": "検索の対象は、名前、作者、説明、タグです。",
    "catalog.unreadable": "カタログを読み込めません",
    "catalog.unreadableText": "接続を確認して、再読み込みしてください。インストール済みのものは「インストール済み」に残ります。",
    "catalog.offline": "いまはカタログを読み込めません。インストール済みのコピーを表示しています。",
    "empty.title": "まだ何もありません", "page": "{page} / {pages} ページ",
    "status.reading": "カタログを読み込み中…", "status.working": "処理中…",
    "status.unreadable": "Ampify の状態を読み取れません： {error}", "status.unreadableReply": "読み取れない応答",
    "status.failed": "失敗しました： {error}", "status.failedCode": "失敗しました（コード {code}）",
    "done.installedInUse": "{name} をインストールして使用中です", "done.installedOn": "{name} をインストールしてオンにしました",
    "done.installedUse": "{name} をインストールしました。できることを確認してから使ってください。",
    "done.installedTurnOn": "{name} をインストールしました。できることを確認してからオンにしてください。",
    "done.updated": "{name} を {version} に更新しました", "done.inUse": "{name} を使用中です",
    "done.inUseScheme": "{name} を使用中（バリエーション： {scheme}）", "done.appleLook": "Apple Music 本来の見た目に戻しました",
    "done.on": "{name} をオンにしました", "done.off": "{name} をオフにしました", "done.removed": "{name} をアンインストールしました",
    "preview": "{name} のプレビュー",
    "detail.settings": "設定", "detail.settingsNote": "変更はすぐに反映されます。Apple Music の設定の「Ampify」にもあります。", "done.setting": "{name}：{setting} を変更しました", "action.settingsOf": "{name} の設定",
    "detail.readmeLoading": "説明を読み込んでいます…",
    "action.settings": "設定", "action.openSettings": "Ampify の設定を開く", "detail.can": "できること", "detail.about": "情報", "detail.kind": "種類", "detail.author": "作者", "detail.version": "バージョン", "detail.tags": "タグ", "detail.gone": "この要素はカタログにもインストール済みにもありません。", "detail.noDescription": "説明はまだありません。",
  },
  zh: {
    "tab.theme": "主题", "tab.extension": "扩展", "tab.app": "应用", "tab.snippet": "代码片段", "tab.installed": "已安装",
    "empty.theme": "目录中暂时没有主题。",
    "empty.extension": "目录中暂时没有扩展。",
    "empty.app": "目录中暂时没有应用。",
    "empty.snippet": "目录中暂时没有代码片段。代码片段就是几行 user.css：ampify new snippet 可以创建一个。",
    "empty.installed": "还没有安装任何内容。请在其他标签页中选择主题或扩展。",
    "kind.theme": "主题", "kind.palette": "配色", "kind.extension": "扩展", "kind.app": "应用", "kind.snippet": "代码片段",
    "by": "作者：{author}", "version": "版本 {version}",
    "state.broken": "无法加载，请查看 ampify doctor", "state.installed": "已安装", "state.inUse": "使用中",
    "state.on": "已开启", "state.off": "已关闭", "state.update": "更新到 {version}",
    "action.install": "安装", "action.update": "更新", "action.use": "使用", "action.turnOff": "关闭",
    "action.turnOn": "开启", "action.details": "详情", "action.uninstall": "卸载",
    "action.confirmUninstall": "确认卸载", "action.refresh": "刷新",
    "action.clearSearch": "清除搜索", "action.appleLook": "恢复 Apple Music 原本的外观",
    "action.previous": "上一页", "action.next": "下一页",
    "uninstall.themeFirst": "请先关闭该主题",
    "perm.none": "不需要任何权限。", "perm.can": "它可以：", "perm.one": "需要 1 项权限", "perm.count": "需要 {count} 项权限",
    "perm.new": "版本 {version} 新增",
    "scheme": "配色方案", "tags": "标签：{tags}",
    "theme.none": "没有主题：Apple Music 原本的外观。", "theme.inUse": "正在使用的主题：{name}",
    "search.theme": "搜索主题", "search.extension": "搜索扩展", "search.app": "搜索应用",
    "search.snippet": "搜索代码片段", "search.installed": "在已安装的内容中搜索",
    "search.none": "没有与“{search}”匹配的内容",
    "search.where": "搜索范围包括名称、作者、描述和标签。",
    "catalog.unreadable": "无法读取目录",
    "catalog.unreadableText": "请检查网络连接，然后刷新。已安装的内容仍在“已安装”中。",
    "catalog.offline": "暂时无法读取目录：这里显示的是已安装的副本。",
    "empty.title": "这里还没有内容", "page": "第 {page} 页，共 {pages} 页",
    "status.reading": "正在读取目录…", "status.working": "正在处理…",
    "status.unreadable": "无法读取 Ampify 的状态：{error}", "status.unreadableReply": "无法识别的响应",
    "status.failed": "失败：{error}", "status.failedCode": "失败（代码 {code}）",
    "done.installedInUse": "{name} 已安装并正在使用", "done.installedOn": "{name} 已安装并开启",
    "done.installedUse": "{name} 已安装。请先看看它能做什么，再使用它。",
    "done.installedTurnOn": "{name} 已安装。请先看看它能做什么，再开启它。",
    "done.updated": "{name} 已更新到 {version}", "done.inUse": "正在使用 {name}",
    "done.inUseScheme": "正在使用 {name}，配色方案：{scheme}", "done.appleLook": "已恢复 Apple Music 原本的外观",
    "done.on": "{name} 已开启", "done.off": "{name} 已关闭", "done.removed": "{name} 已卸载",
    "preview": "{name} 的预览",
    "detail.settings": "设置", "detail.settingsNote": "更改会立即生效。也可以在 Apple Music 的“设置”中的 Ampify 下找到。", "done.setting": "{name}：已更改 {setting}", "action.settingsOf": "{name} 的设置",
    "detail.readmeLoading": "正在读取介绍…",
    "action.settings": "设置", "action.openSettings": "打开 Ampify 设置", "detail.can": "它可以做什么", "detail.about": "关于", "detail.kind": "类型", "detail.author": "作者", "detail.version": "版本", "detail.tags": "标签", "detail.gone": "该内容已不在目录中，也未安装。", "detail.noDescription": "暂无描述。",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);

// One sentence per permission, shown before the installation and before turning it on.
const PERMISSIONS = {
  en: {
    "player.read": "see the track that is playing",
    "player.control": "play, pause and change track",
    "audio.analyze": "analyze the sound (tempo, volume)",
    "audio.gain": "make the sound louder or quieter",
    "audio.eq": "set an equalizer",
    storage: "keep its settings",
    notifications: "show notifications",
    "ipc.discord": "talk to Discord (status)",
    "midi.input": "read a MIDI controller",
    "library.read": "read the library",
    "library.write": "change the library",
    "services.read": "read the state of Ampify's services",
    "ui.keyboard": "add keyboard shortcuts",
    "ui.topbar": "add a button at the top",
    "ui.playbar": "add a button in the playback bar",
    "ui.sidebar": "add an entry to the sidebar",
    "ui.menu": "add menu entries",
    "ui.popup": "open pop-up windows",
    "ui.panel": "show a panel on the right",
    "ui.page": "add a page",
    "ui.window": "open a window",
    "ui.tray": "add an icon near the clock",
    "ui.fullscreen": "cover the window with a full-screen view",
    "fs.output": "write text files in its Ampify folder",
    "kit.manage": "install and remove elements through ampify",
    "clipboard.read": "read what you copied",
    "clipboard.write": "copy text to the clipboard",
    network: "send requests to {host}",
  },
  fr: {
    "player.read": "voir le morceau en cours",
    "player.control": "lancer, mettre en pause et changer de morceau",
    "audio.analyze": "analyser le son (tempo, volume)",
    "audio.gain": "rendre le son plus fort ou plus faible",
    "audio.eq": "régler un égaliseur",
    storage: "garder ses réglages",
    notifications: "afficher des notifications",
    "ipc.discord": "parler à Discord (statut)",
    "midi.input": "lire un contrôleur MIDI",
    "library.read": "lire la bibliothèque",
    "library.write": "modifier la bibliothèque",
    "services.read": "lire l'état des services d'Ampify",
    "ui.keyboard": "ajouter des raccourcis clavier",
    "ui.topbar": "ajouter un bouton en haut",
    "ui.playbar": "ajouter un bouton dans la barre de lecture",
    "ui.sidebar": "ajouter une entrée dans la barre latérale",
    "ui.menu": "ajouter des entrées de menu",
    "ui.popup": "ouvrir des fenêtres surgissantes",
    "ui.panel": "afficher un panneau à droite",
    "ui.page": "ajouter une page",
    "ui.window": "ouvrir une fenêtre",
    "ui.tray": "ajouter une icône près de l'horloge",
    "ui.fullscreen": "couvrir la fenêtre d'une vue plein écran",
    "fs.output": "écrire des fichiers texte dans son dossier Ampify",
    "kit.manage": "installer et retirer des éléments avec ampify",
    "clipboard.read": "lire ce que vous avez copié",
    "clipboard.write": "copier du texte dans le presse-papiers",
    network: "envoyer des requêtes à {host}",
  },
  de: {
    "player.read": "den aktuell laufenden Titel sehen",
    "player.control": "wiedergeben, pausieren und den Titel wechseln",
    "audio.analyze": "den Klang analysieren (Tempo, Lautstärke)",
    "audio.gain": "den Klang lauter oder leiser machen",
    "audio.eq": "einen Equalizer einstellen",
    storage: "seine Einstellungen speichern",
    notifications: "Benachrichtigungen anzeigen",
    "ipc.discord": "mit Discord sprechen (Status)",
    "midi.input": "einen MIDI-Controller auslesen",
    "library.read": "die Mediathek lesen",
    "library.write": "die Mediathek ändern",
    "services.read": "den Zustand der Ampify-Dienste lesen",
    "ui.keyboard": "Tastenkürzel hinzufügen",
    "ui.topbar": "oben eine Schaltfläche hinzufügen",
    "ui.playbar": "eine Schaltfläche in der Wiedergabeleiste hinzufügen",
    "ui.sidebar": "einen Eintrag in der Seitenleiste hinzufügen",
    "ui.menu": "Menüeinträge hinzufügen",
    "ui.popup": "Popup-Fenster öffnen",
    "ui.panel": "rechts ein Panel anzeigen",
    "ui.page": "eine Seite hinzufügen",
    "ui.window": "ein Fenster öffnen",
    "ui.tray": "ein Symbol neben der Uhr hinzufügen",
    "ui.fullscreen": "das Fenster mit einer Vollbildansicht überdecken",
    "fs.output": "Textdateien in seinen Ampify-Ordner schreiben",
    "kit.manage": "Elemente über ampify installieren und entfernen",
    "clipboard.read": "lesen, was Sie kopiert haben",
    "clipboard.write": "Text in die Zwischenablage kopieren",
    network: "Anfragen an {host} senden",
  },
  es: {
    "player.read": "ver la canción que suena",
    "player.control": "reproducir, pausar y cambiar de canción",
    "audio.analyze": "analizar el sonido (tempo, volumen)",
    "audio.gain": "subir o bajar el volumen del sonido",
    "audio.eq": "ajustar un ecualizador",
    storage: "guardar sus ajustes",
    notifications: "mostrar notificaciones",
    "ipc.discord": "hablar con Discord (estado)",
    "midi.input": "leer un controlador MIDI",
    "library.read": "leer la biblioteca",
    "library.write": "modificar la biblioteca",
    "services.read": "leer el estado de los servicios de Ampify",
    "ui.keyboard": "añadir atajos de teclado",
    "ui.topbar": "añadir un botón arriba",
    "ui.playbar": "añadir un botón en la barra de reproducción",
    "ui.sidebar": "añadir una entrada en la barra lateral",
    "ui.menu": "añadir entradas de menú",
    "ui.popup": "abrir ventanas emergentes",
    "ui.panel": "mostrar un panel a la derecha",
    "ui.page": "añadir una página",
    "ui.window": "abrir una ventana",
    "ui.tray": "añadir un icono junto al reloj",
    "ui.fullscreen": "cubrir la ventana con una vista a pantalla completa",
    "fs.output": "escribir archivos de texto en su carpeta de Ampify",
    "kit.manage": "instalar y quitar elementos con ampify",
    "clipboard.read": "leer lo que ha copiado",
    "clipboard.write": "copiar texto al portapapeles",
    network: "enviar solicitudes a {host}",
  },
  pt: {
    "player.read": "ver a faixa que está tocando",
    "player.control": "reproduzir, pausar e trocar de faixa",
    "audio.analyze": "analisar o som (tempo, volume)",
    "audio.gain": "deixar o som mais alto ou mais baixo",
    "audio.eq": "ajustar um equalizador",
    storage: "guardar suas configurações",
    notifications: "mostrar notificações",
    "ipc.discord": "conversar com o Discord (status)",
    "midi.input": "ler um controlador MIDI",
    "library.read": "ler a biblioteca",
    "library.write": "modificar a biblioteca",
    "services.read": "ler o estado dos serviços do Ampify",
    "ui.keyboard": "adicionar atalhos de teclado",
    "ui.topbar": "adicionar um botão no alto",
    "ui.playbar": "adicionar um botão na barra de reprodução",
    "ui.sidebar": "adicionar uma entrada na barra lateral",
    "ui.menu": "adicionar itens de menu",
    "ui.popup": "abrir janelas pop-up",
    "ui.panel": "mostrar um painel à direita",
    "ui.page": "adicionar uma página",
    "ui.window": "abrir uma janela",
    "ui.tray": "adicionar um ícone perto do relógio",
    "ui.fullscreen": "cobrir a janela com uma visualização em tela cheia",
    "fs.output": "gravar arquivos de texto em sua pasta do Ampify",
    "kit.manage": "instalar e remover elementos pelo ampify",
    "clipboard.read": "ler o que você copiou",
    "clipboard.write": "copiar texto para a área de transferência",
    network: "enviar solicitações para {host}",
  },
  ru: {
    "player.read": "видеть, какой трек сейчас играет",
    "player.control": "запускать, ставить на паузу и переключать треки",
    "audio.analyze": "анализировать звук (темп, громкость)",
    "audio.gain": "делать звук громче или тише",
    "audio.eq": "настраивать эквалайзер",
    storage: "сохранять свои настройки",
    notifications: "показывать уведомления",
    "ipc.discord": "общаться с Discord (статус)",
    "midi.input": "считывать MIDI-контроллер",
    "library.read": "читать медиатеку",
    "library.write": "изменять медиатеку",
    "services.read": "читать состояние служб Ampify",
    "ui.keyboard": "добавлять сочетания клавиш",
    "ui.topbar": "добавлять кнопку вверху",
    "ui.playbar": "добавлять кнопку на панель воспроизведения",
    "ui.sidebar": "добавлять пункт в боковую панель",
    "ui.menu": "добавлять пункты меню",
    "ui.popup": "открывать всплывающие окна",
    "ui.panel": "показывать панель справа",
    "ui.page": "добавлять страницу",
    "ui.window": "открывать окно",
    "ui.tray": "добавлять значок рядом с часами",
    "ui.fullscreen": "закрывать окно полноэкранным представлением",
    "fs.output": "записывать текстовые файлы в свою папку Ampify",
    "kit.manage": "устанавливать и удалять элементы через ampify",
    "clipboard.read": "читать то, что вы скопировали",
    "clipboard.write": "копировать текст в буфер обмена",
    network: "отправлять запросы на {host}",
  },
  ja: {
    "player.read": "再生中の曲を見る",
    "player.control": "再生、一時停止、曲の切り替えをする",
    "audio.analyze": "音を解析する（テンポ、音量）",
    "audio.gain": "音を大きくしたり小さくしたりする",
    "audio.eq": "イコライザーを設定する",
    storage: "自分の設定を保存する",
    notifications: "通知を表示する",
    "ipc.discord": "Discord と連携する（ステータス）",
    "midi.input": "MIDI コントローラーを読み取る",
    "library.read": "ライブラリを読む",
    "library.write": "ライブラリを変更する",
    "services.read": "Ampify のサービスの状態を読む",
    "ui.keyboard": "キーボードショートカットを追加する",
    "ui.topbar": "上部にボタンを追加する",
    "ui.playbar": "再生バーにボタンを追加する",
    "ui.sidebar": "サイドバーに項目を追加する",
    "ui.menu": "メニュー項目を追加する",
    "ui.popup": "ポップアップウィンドウを開く",
    "ui.panel": "右側にパネルを表示する",
    "ui.page": "ページを追加する",
    "ui.window": "ウィンドウを開く",
    "ui.tray": "時計の近くにアイコンを追加する",
    "ui.fullscreen": "ウィンドウ全体をフルスクリーン表示でおおう",
    "fs.output": "自分の Ampify フォルダにテキストファイルを書き込む",
    "kit.manage": "ampify を通じて要素をインストール・削除する",
    "clipboard.read": "コピーした内容を読み取る",
    "clipboard.write": "テキストをクリップボードにコピーする",
    network: "{host} にリクエストを送る",
  },
  zh: {
    "player.read": "查看正在播放的歌曲",
    "player.control": "播放、暂停和切换歌曲",
    "audio.analyze": "分析声音（节奏、音量）",
    "audio.gain": "调高或调低声音",
    "audio.eq": "设置均衡器",
    storage: "保存自己的设置",
    notifications: "显示通知",
    "ipc.discord": "与 Discord 通信（状态）",
    "midi.input": "读取 MIDI 控制器",
    "library.read": "读取资料库",
    "library.write": "修改资料库",
    "services.read": "读取 Ampify 各项服务的状态",
    "ui.keyboard": "添加键盘快捷键",
    "ui.topbar": "在顶部添加按钮",
    "ui.playbar": "在播放栏中添加按钮",
    "ui.sidebar": "在侧边栏中添加条目",
    "ui.menu": "添加菜单项",
    "ui.popup": "打开弹出窗口",
    "ui.panel": "在右侧显示面板",
    "ui.page": "添加页面",
    "ui.window": "打开窗口",
    "ui.tray": "在时钟旁添加图标",
    "ui.fullscreen": "用全屏视图覆盖窗口",
    "fs.output": "在自己的 Ampify 文件夹中写入文本文件",
    "kit.manage": "通过 ampify 安装和移除内容",
    "clipboard.read": "读取你复制的内容",
    "clipboard.write": "将文本复制到剪贴板",
    network: "向 {host} 发送请求",
  },
};
const permissionTexts = PERMISSIONS[Ampify.i18n.language] || PERMISSIONS.en;
const fill = (pattern, values) => pattern.replace(/\{(\w+)\}/g, (all, name) => (name in values ? values[name] : all));

const describePermission = (permission) =>
  permission.startsWith("network:")
    ? fill(permissionTexts.network, { host: permission.slice(8) })
    : permissionTexts[permission] || PERMISSIONS.en[permission] || permission;

const TABS = ["theme", "extension", "app", "snippet", "installed"];
const KIND_ORDER = ["theme", "extension", "app", "snippet"];
// The kit's icon of each kind (Ampify.icons), for an element without a preview picture.
const KIND_ICONS = { theme: "drop", extension: "puzzle", app: "apps", snippet: "code" };
const COLUMNS = 3;
const PAGE_SIZE = 12;  // four rows of cards keep the page far below the kernel's 300 elements
const PICTURE_HEIGHT = 180;
const BAND_HEIGHT = 72;  // a row of cards without any picture gets a short band with the kind's icon
const SHOWCASE_HEIGHT = 360;  // the picture at the top of a detail page
const TILE_SIZE = 160;        // the icon tile of a detail page without a picture
const DETAIL_WIDTH = 1000;    // as wide as Apple's settings rows: a wider page leaves the eye far from the buttons
const SETTINGS_GLYPH = "";  // Settings, in Segoe Fluent Icons

// The last useful line of ampify's output, for the status line.
const lastLine = (text) => String(text || "").trim().split(/\r?\n/).filter(Boolean).pop() || "";

// True when version a is above version b: "0.10.0" is above "0.9.1".
function newer(a, b) {
  const left = String(a).split(".").map(Number);
  const right = String(b).split(".").map(Number);
  for (let i = 0; i < Math.max(left.length, right.length); i++) {
    if ((left[i] || 0) !== (right[i] || 0)) return (left[i] || 0) > (right[i] || 0);
  }
  return false;
}

// The catalog entry and the installed copy of one element, as one card. The installed picture comes first: it is local.
function merge(entry, installed) {
  const source = entry || installed;
  const update = Boolean(entry && installed && newer(entry.version, installed.version));
  // The translation for Windows' language, from the catalog or else the installed copy (a catalog published before the
  // element was translated has none), else the English name and description.
  const translationOf = (copy) => (copy && copy.translations && copy.translations[Ampify.i18n.language]) || null;
  const translated = translationOf(entry) || translationOf(installed) || {};
  return {
    key: source.kind + ":" + source.id,
    id: source.id,
    kind: source.kind,
    name: translated.name || source.name || source.id,
    // The catalog's own name: an install that brings a translation does not move the card.
    sortName: source.name || source.id,
    author: source.author || "",
    version: (installed || entry).version || "",
    description: translated.description || source.description || "",
    tags: source.tags || [],
    // The permissions of the copy a click acts on: the installed one, or the catalog's for an install or an update.
    permissions: (installed && !update ? installed : entry).permissions || [],
    preview: (installed && installed.preview) || (entry && entry.preview) || "",
    entry,
    installed,
    update,
  };
}

// A theme that only brings colours, by its "colors" tag: the card says so, as Spicetify's colour schemes.
const kindLabel = (item) =>
  t(item.kind === "theme" && item.tags.includes("colors") ? "kind.palette" : "kind." + item.kind);

// Every element once: the catalog, then what is installed without being in it (a local or development copy).
function itemsOf(state) {
  const installed = state.installed.filter((item) => !(item.kind === "app" && item.id === "marketplace"));
  const keyOf = (item) => item.kind + ":" + item.id;
  const copies = new Map(installed.map((item) => [keyOf(item), item]));
  const items = state.catalog.map((entry) => merge(entry, copies.get(keyOf(entry))));
  const listed = new Set(state.catalog.map(keyOf));
  installed.filter((item) => !listed.has(keyOf(item))).forEach((item) => items.push(merge(null, item)));
  return items.sort((a, b) => a.sortName.localeCompare(b.sortName));
}

const matches = (item, search) => {
  const wanted = search.trim().toLowerCase();
  return !wanted || [item.name, item.id, item.description, item.author, item.tags.join(" ")]
    .some((field) => String(field).toLowerCase().includes(wanted));
};

// What a card says about the element next to its button. Empty when it is not installed.
function stateOf(item, activeTheme) {
  if (!item.installed) return "";
  if (!item.installed.valid) return t("state.broken");
  const update = item.update ? t("state.update", { version: item.entry.version }) : "";
  if (item.kind === "theme" && item.id !== activeTheme) return update || t("state.installed");
  const now = t(item.kind === "theme" ? "state.inUse" : item.installed.enabled ? "state.on" : "state.off");
  return update ? now + " · " + update : now;
}

// The permissions the catalog's version adds to the installed copy.
function addedPermissions(item) {
  if (!item.update) return [];
  const had = new Set(item.installed.permissions || []);
  return item.permissions.filter((permission) => !had.has(permission));
}

// The permissions the main action of an installed element grants: all of them when it turns the element on, the
// added ones when it updates an element that is on.
function permissionsToGrant(item, activeTheme) {
  const on = item.kind === "theme" ? item.id === activeTheme : item.installed.enabled;
  if (item.update) return on ? addedPermissions(item) : [];
  return on ? [] : item.permissions;
}


// The scheme a menu shows: the one in use when the theme has it ("auto" or a stale name does not), else its default.
function shownScheme(theme, scheme) {
  const schemes = theme.installed.schemes || [];
  if (schemes.includes(scheme)) return scheme;
  return schemes.includes(theme.installed.defaultScheme) ? theme.installed.defaultScheme : schemes[0];
}

// The element's whole picture, never cut, on a faint band when its proportions differ from the card's; without one, a
// band with the icon of its kind.
function Preview({ item, height, onClick }) {
  if (item.preview) {
    return h("stack", { height, cornerRadius: 6, background: "#14808080", align: "stretch" },
      h("image", { src: item.preview, label: t("preview", { name: item.name }), fit: "contain", grow: true,
                   align: "stretch", onClick }));
  }
  // A faint grey that reads on light and dark cards alike.
  return h("stack", { height, cornerRadius: 6, background: "#14808080", align: "stretch" },
    h("icon", { icon: Ampify.icons.get(KIND_ICONS[item.kind]), size: height > BAND_HEIGHT ? 32 : 24, label: kindLabel(item),
                align: "center", valign: "center", grow: true }));
}

// The main action of an element, from its state: install, update, use a theme, or turn it on and off. On a card, an
// action that grants permissions opens the detail view instead, where they are all written; its label ends with an
// ellipsis, as a Windows button that asks more before acting.
function MainAction({ item, activeTheme, busy, act, open }) {
  if (!item.installed) return h(ButtonPrimary, { label: t("action.install"), disabled: busy, onClick: () => act.install(item) });
  const ask = Boolean(open) && permissionsToGrant(item, activeTheme).length > 0;
  const primary = (label, onClick) =>
    h(ButtonPrimary, { label: ask ? label + "…" : label, disabled: busy, onClick: ask ? open : onClick });
  if (item.update) return primary(t("action.update"), () => act.update(item));
  if (!item.installed.valid) return null;
  if (item.kind === "theme") {
    return item.id === activeTheme
      ? h(Button, { label: t("action.turnOff"), disabled: busy, onClick: act.clearTheme })
      : primary(t("action.use"), () => act.use(item));
  }
  return item.installed.enabled
    ? h(Button, { label: t("action.turnOff"), disabled: busy, onClick: () => act.toggle(item, false) })
    : primary(t("action.turnOn"), () => act.toggle(item, true));
}

// A card: picture, name, who made it, two lines of description, tags; then the action, the state and Details. The
// permissions are listed in the detail view; the card only counts them, and says nothing when there are none.
function ItemCard({ item, activeTheme, busy, act, open, previewHeight }) {
  const meta = [kindLabel(item), item.author && t("by", { author: item.author }), item.version && "v" + item.version]
    .filter(Boolean).join(" · ");
  const state = stateOf(item, activeTheme);
  const count = item.permissions.length;
  // Every line keeps its height, so the buttons of a row of cards line up.
  return h(Card, { grow: true, valign: "stretch", padding: 14 },
    h(Preview, { item, height: previewHeight, onClick: open }),
    h(Text, { weight: 600, wrap: false }, item.name),
    h(Text, { variant: "caption", wrap: false }, meta),
    h("stack", { height: 34 }, h(Text, { variant: "caption", maxLines: 2 }, textPieces(item.description)[0])),
    h(Text, { variant: "caption", wrap: false }, [item.tags.slice(0, 3).join(" · "),
                                                  count ? t(count === 1 ? "perm.one" : "perm.count", { count }) : ""].filter(Boolean).join("   ") || " "),
    h("row", { gap: 10 },
      h(MainAction, { item, activeTheme, busy, act, open }),
      state ? h(Text, { variant: "caption", valign: "center", wrap: false }, state) : null,
      h("stack", { grow: true }),
      item.installed && (item.installed.settings || []).length
        ? h(ButtonTertiary, { icon: SETTINGS_GLYPH, tooltip: t("action.settingsOf", { name: item.name }),
                              onClick: () => openItem(item, "", true) })
        : null,
      h(ButtonTertiary, { label: t("action.details"), onClick: open })));
}

function Grid({ items, renderCard }) {
  const rows = [];
  for (let start = 0; start < items.length; start += COLUMNS) {
    const row = items.slice(start, start + COLUMNS);
    const previewHeight = row.some((item) => item.preview) ? PICTURE_HEIGHT : BAND_HEIGHT;
    const cells = row.map((item) => renderCard(item, previewHeight));
    while (cells.length < COLUMNS) cells.push(h("stack", { key: "filler" + cells.length, grow: true }));
    rows.push(h("row", { key: "row" + start, gap: 16 }, cells));
  }
  return h("stack", { gap: 16 }, rows);
}

// The element's picture at full width, or without one a square tile with the icon of its kind.
function Showcase({ item }) {
  if (item.preview) {
    return h("image", { src: item.preview, label: t("preview", { name: item.name }), fit: "contain", height: SHOWCASE_HEIGHT,
                        cornerRadius: 8, align: "stretch" });
  }
  return h("stack", { width: TILE_SIZE, height: TILE_SIZE, cornerRadius: 12, background: "#14808080", valign: "start" },
    h("icon", { icon: Ampify.icons.get(KIND_ICONS[item.kind]), size: 64, label: kindLabel(item), align: "center",
                valign: "center", grow: true }));
}

// One line of the "About" card: what it is on the left, its value on the right.
const Fact = ({ label, value }) =>
  h("row", { gap: 12 },
    h(Text, { variant: "caption", wrap: false, valign: "center" }, label),
    h(Text, { grow: true, align: "right", valign: "center" }, value));

// The kernel takes 500 bytes of UTF-8 at most in one text: a longer one is drawn as several, cut at a space when the
// piece has one.
const TEXT_MAX_BYTES = 500;
const utf8Length = (char) => {
  const code = char.codePointAt(0);
  return code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
};

function textPieces(text) {
  const pieces = [];
  let piece = "";
  let size = 0;
  for (const char of text) {
    const bytes = utf8Length(char);
    // A cut at a space may leave a rest still too long for this character: cut again, at worst where it stands.
    while (size + bytes > TEXT_MAX_BYTES) {
      const space = piece.lastIndexOf(" ");
      pieces.push(space > 0 ? piece.slice(0, space) : piece);
      piece = space > 0 ? piece.slice(space + 1) : "";
      size = Array.from(piece).reduce((total, rest) => total + utf8Length(rest), 0);
    }
    piece += char;
    size += bytes;
  }
  pieces.push(piece);
  return pieces;
}

// One text, or a column of them when it is too long for one.
function LongText({ props, text }) {
  const pieces = textPieces(text);
  if (pieces.length === 1) return h(Text, props, text);
  return h("stack", null, pieces.map((piece, index) => h(Text, { ...props, key: "p" + index }, piece)));
}

// The README of an element, as `ampify market readme` gives it: read once per version ("" when it has none), the
// reading itself kept while it runs so two pages opened in a row share it. A failed reading is not kept: the next
// page tries again.
const readmes = new Map();

function readReadme(item, key) {
  if (!readmes.has(key)) {
    const reading = Ampify.kit.run(["market", "readme", item.id])
      .then((result) => (result.code === 0 ? result.out : null), () => null)
      .then((text) => {
        if (text === null) readmes.delete(key);
        else readmes.set(key, text);
        return text || "";
      });
    readmes.set(key, reading);
  }
  return Promise.resolve(readmes.get(key));
}

// The README to show: null while it is read.
function useReadme(item) {
  // ampify reads the installed copy's, or the catalog's when an update waits: both versions name the one shown.
  const key = [item.key, item.entry && item.entry.version, item.installed && item.installed.version].join("@");
  const known = readmes.get(key);
  const [text, setText] = useState(typeof known === "string" ? known : null);
  useEffect(() => {
    if (text === null) readReadme(item, key).then(setText);
  }, []);
  return text;
}

// The kernel draws 300 elements at most on a page: a README stops at this many, leaving room for the page around it.
const README_MAX_NODES = 150;
const RULE_LINE = /^\s*([-*_])(\s*\1){2,}\s*$/;  // ---, *** or ___: a horizontal rule

// Markdown to blocks the kit can draw: headings, paragraphs, list items, code. Pictures and HTML are left out (the
// preview is the picture); a table row reads as its cells side by side.
function readmeBlocks(markdown) {
  const blocks = [];
  let paragraph = [];
  let code = null;
  const flush = () => {
    if (paragraph.length) blocks.push({ kind: "paragraph", text: paragraph.join(" ") });
    paragraph = [];
  };
  for (const line of markdown.split("\n")) {
    const fence = /^\s*(```|~~~)/.test(line);
    if (code) {
      if (fence) blocks.push({ kind: "code", text: code.join("\n") });
      code = fence ? null : code.concat(line);
      continue;
    }
    const heading = /^(#{1,6})\s+(.*?)[\s#]*$/.exec(line);
    const item = /^\s*([-*+]|\d+[.)])\s+(.*)$/.exec(line);
    const last = blocks[blocks.length - 1];
    if (fence || heading || item || !line.trim() || RULE_LINE.test(line)) flush();
    if (fence) code = [];
    else if (heading) blocks.push({ kind: "heading", level: heading[1].length, text: heading[2] });
    else if (item) blocks.push({ kind: "item", mark: /\d/.test(item[1]) ? item[1].replace(")", ".") : "•", text: item[2] });
    else if (/^\s{2,}\S/.test(line) && !paragraph.length && last && last.kind === "item") last.text += " " + line.trim();
    else if (/^\s*\|.*\|\s*$/.test(line)) {
      flush();
      if (!/^[\s|:-]+$/.test(line)) blocks.push({ kind: "paragraph", text: line.trim().slice(1, -1).split("|").map((cell) => cell.trim()).join("  ·  ") });
    } else if (line.trim() && !RULE_LINE.test(line)) paragraph.push(line.replace(/^\s*>\s?/, "").trim());
  }
  if (code) blocks.push({ kind: "code", text: code.join("\n") });
  flush();
  return blocks;
}

// The words of a line without its marks: a link keeps its text, bold and code their words, a picture or a tag goes.
const plainInline = (text) => text
  .replace(/!\[[^\]]*\]\([^)]*\)/g, "")
  .replace(/\[([^\]]*)\]\([^)]*\)/g, "$1")
  .replace(/<[^>]+>/g, "")
  .replace(/(\*\*|__)(.+?)\1/g, "$2")
  .replace(/\*(\S(?:[^*]*\S)?)\*/g, "$1")
  .replace(/`([^`]+)`/g, "$1")
  .trim();

// The words a block shows: code as written, the rest without its marks, a list item after its bullet or number.
function shownText(block) {
  if (block.kind === "code") return block.text;
  const text = plainInline(block.text);
  return block.kind === "item" && text ? block.mark + " " + text : text;
}

// The elements a block costs the kernel: one per piece of text, one more for the column of a long one and for the
// background of code.
function nodesOf(block) {
  const text = shownText(block);
  if (!text && block.kind !== "code") return 0;
  const pieces = textPieces(text).length;
  return pieces + (pieces > 1 ? 1 : 0) + (block.kind === "code" ? 1 : 0);
}

function ReadmeBlock({ block }) {
  const text = shownText(block);
  if (block.kind === "code") {
    return h("stack", { background: "#14808080", cornerRadius: 6, padding: 12 }, h(LongText, { text }));
  }
  if (!text) return null;
  if (block.kind === "heading") {
    return h(LongText, { props: { variant: block.level <= 2 ? "subtitle" : undefined, weight: 600 }, text });
  }
  if (block.kind === "item") return h(LongText, { text });
  return h(LongText, { props: { weight: /^\s*(\*\*|__).*\1\s*$/.test(block.text) ? 600 : undefined }, text });
}

// The README below the facts, as Spicetify's Marketplace shows it; nothing at all for an element without one. It
// stops where the kernel's 300 elements would be reached with the page around it.
function Readme({ item }) {
  const text = useReadme(item);
  if (text === null) return h(Text, { variant: "caption" }, t("detail.readmeLoading"));
  const blocks = readmeBlocks(text);
  if (!blocks.length) return null;
  const shown = [];
  let budget = README_MAX_NODES;
  for (const block of blocks) {
    budget -= nodesOf(block);
    if (budget < 0) break;
    shown.push(block);
  }
  return h(Card, { padding: 24 }, h("stack", { gap: 12 },
    shown.map((block, index) => h(ReadmeBlock, { key: "b" + index, block })),
    shown.length < blocks.length ? h(Text, { key: "more", variant: "caption" }, "…") : null));
}

// A setting's label and option texts in Windows' language, from the element's translations, else the manifest's.
function settingTexts(item) {
  const copy = item.installed || {};
  const translated = (copy.translations && copy.translations[Ampify.i18n.language]) || {};
  return {
    label: (setting) => (translated.settings && translated.settings[setting.key]) || setting.label,
    option: (value) => (translated.options && translated.options[value]) || value,
  };
}

const SETTING_DELAY_MS = 600;  // a slider or a field is written once the hand stops, not at every step
const pendingSettings = new Map();

function changeLater(key, change) {
  clearTimeout(pendingSettings.get(key));
  pendingSettings.set(key, setTimeout(() => {
    pendingSettings.delete(key);
    change();
  }, SETTING_DELAY_MS));
}

// One declared setting as the control of its type: a switch, a menu, a slider when a number has both bounds, else a
// field. The control shows the new value at once; ampify writes it, then the state is read again.
function SettingControl({ item, setting, texts, act }) {
  const [value, setValue] = useState(setting.value);
  const label = texts.label(setting);
  const later = (next) => {
    setValue(next);
    changeLater(item.key + ":" + setting.key, () => act.setting(item, setting, label, String(next)));
  };
  if (setting.type === "toggle") {
    return h(Toggle, { label, checked: value === "true" || value === true,
                       onChange: (on) => { setValue(String(on)); act.setting(item, setting, label, String(on)); } });
  }
  if (setting.type === "choice") {
    return h(Dropdown, { label, value, options: setting.options.map((option) => ({ value: option, label: texts.option(option) })),
                         onChange: (next) => { setValue(next); act.setting(item, setting, label, next); } });
  }
  if (setting.type === "number" && setting.min !== undefined && setting.max !== undefined) {
    const step = Number.isInteger(setting.min) && Number.isInteger(setting.max) && setting.max - setting.min > 4 ? 1 : 0.05;
    // The slider's own header carries the label and its value, in the same text as the menus' and switches' labels.
    return h(Slider, { label, min: setting.min, max: setting.max, step, value: Number(value), onChange: later });
  }
  return h(TextField, { label, value, onChange: later });
}

// The element's settings on its own page, changed in place: the same ones as in Apple Music's Settings.
function SettingsPanel({ item, act, named }) {
  const settings = (item.installed && item.installed.settings) || [];
  if (!settings.length) return null;
  const texts = settingTexts(item);
  return h(Card, { title: named ? t("action.settingsOf", { name: item.name }) : t("detail.settings"), padding: 16 },
    h("stack", { gap: 12 },
      h(Text, { key: "note", variant: "caption" }, t("detail.settingsNote")),
      settings.map((setting) => h(SettingControl, { key: setting.key, item, setting, texts, act }))));
}

// The detail page of an element: its name, what it does and its actions first, then what it may do and where it
// comes from, so a page without a picture or a long description still reads as one, then its README.
function Details({ item, activeTheme, scheme, busy, act, message }) {
  const [confirm, setConfirm] = useState(false);
  const schemes = (item.installed && item.installed.schemes) || [];
  const active = item.kind === "theme" && item.id === activeTheme;
  const removable = item.installed && !active;
  const state = stateOf(item, activeTheme);
  const added = new Set(addedPermissions(item));

  const actions = h("row", { gap: 8 },
    h(MainAction, { item, activeTheme, busy, act }),
    !item.installed ? null
      : confirm
        ? h(Button, { label: t("action.confirmUninstall"), disabled: busy,
                      onClick: () => act.remove(item) })
        : h(ButtonTertiary, { label: t("action.uninstall"), disabled: busy || !removable,
                              tooltip: removable ? "" : t("uninstall.themeFirst"), onClick: () => setConfirm(true) }),
    // The spacer keeps the buttons on the left: a row of two children spreads them apart.
    h("stack", { grow: true }));
  const heading = h("stack", { gap: 8, grow: true, valign: "center" },
    h(Text, { variant: "title" }, item.name),
    h(Text, { variant: "caption" },
      [kindLabel(item), item.author && t("by", { author: item.author }), item.version && t("version", { version: item.version })]
        .filter(Boolean).join(" · ")),
    state ? h(Text, { weight: 600 }, state) : null,
    h(LongText, { text: item.description || t("detail.noDescription") }),
    item.installed && schemes.length > 1
      ? h(Dropdown, { label: t("scheme"), value: shownScheme(item, active ? scheme : ""), options: schemes, disabled: busy,
                      onChange: (value) => act.use(item, value) })
      : null,
    actions);

  const abilities = h(Card, { key: "can", title: t("detail.can"), grow: true, valign: "stretch", padding: 16 },
    item.permissions.length
      ? item.permissions.map((permission) => h(Text, { key: permission },
          "• " + describePermission(permission) + (added.has(permission) ? " (" + t("perm.new", { version: item.entry.version }) + ")" : "")))
      : h(Text, null, t("perm.none")));
  const about = h(Card, { key: "about", title: t("detail.about"), grow: true, valign: "stretch", padding: 16 },
    h(Fact, { label: t("detail.kind"), value: kindLabel(item) }),
    item.author ? h(Fact, { label: t("detail.author"), value: item.author }) : null,
    item.version ? h(Fact, { label: t("detail.version"), value: item.version }) : null,
    item.tags.length ? h(Fact, { label: t("detail.tags"), value: item.tags.join(", ") }) : null,
    item.installed && item.kind !== "snippet"
      ? h("row", { gap: 8 }, h(Button, { label: t("action.openSettings"), onClick: openSettings }), h("stack", { grow: true }))
      : null);

  // Keyed, as on the list: the status line coming and going leaves the rest in place.
  return h("stack", { gap: 20, maxWidth: DETAIL_WIDTH },
    message ? h(Text, { key: "status", variant: "caption" }, message) : null,
    view.settingsFirst ? h(SettingsPanel, { key: "settings", item, act, named: true }) : null,
    item.preview
      ? h("stack", { key: "hero", gap: 20 }, h(Showcase, { item }), heading)
      : h(Card, { key: "hero", padding: 24 }, h("row", { gap: 24 }, h(Showcase, { item }), heading)),
    view.settingsFirst ? null : h(SettingsPanel, { key: "settings", item, act }),
    h("row", { key: "facts", gap: 16 }, abilities, about),
    h(Readme, { key: "readme", item }));
}

function ThemeBar({ items, activeTheme, scheme, busy, act }) {
  if (!activeTheme) return h(Text, { variant: "caption" }, t("theme.none"));
  const theme = items.find((item) => item.kind === "theme" && item.id === activeTheme && item.installed);
  const schemes = (theme && theme.installed.schemes) || [];
  return h("row", { gap: 12 },
    h(Text, { wrap: false, valign: "center" }, t("theme.inUse", { name: theme ? theme.name : activeTheme })),
    theme && schemes.length > 1
      ? h(Dropdown, { value: shownScheme(theme, scheme), options: schemes, disabled: busy,
                      onChange: (value) => act.use(theme, value) })
      : null,
    h("stack", { grow: true }),
    h(ButtonTertiary, { label: t("action.appleLook"), disabled: busy, onClick: act.clearTheme }));
}

function Empty({ title, text, action }) {
  return h("stack", { gap: 8, padding: "48,0,48,0", align: "center" },
    h(Text, { variant: "subtitle", align: "center" }, title),
    h(Text, { variant: "caption", align: "center" }, text),
    action || null);
}

function Pager({ page, pages, setPage }) {
  if (pages <= 1) return null;
  return h("row", { gap: 8 },
    h(ButtonTertiary, { label: t("action.previous"), disabled: page === 0, onClick: () => setPage(page - 1) }),
    h(Text, { variant: "caption", valign: "center" }, t("page", { page: page + 1, pages })),
    h(ButtonTertiary, { label: t("action.next"), disabled: page >= pages - 1, onClick: () => setPage(page + 1) }));
}

// The list and the detail page are two pages of the app, so the arrow of the page header goes from one back to the
// other, as Apple Music's own arrow does. A page mounts anew each time it opens: what it shows first comes from here.
const LIST_PAGE = "/marketplace";
const ITEM_PAGE = "/marketplace/item";
const SETTINGS_PAGE = "/ampify/settings";  // Ampify's own settings page, drawn by the kernel
let lastState = null;
// Where the person was, the element shown in detail, and what an action said before it opened another page.
const view = { tab: "theme", search: "", page: 0, item: "", note: "", settingsFirst: false };
const readers = new Set();  // each mounted page hears a new reading: the list is up to date when the arrow comes back

function publish(state) {
  lastState = state;
  readers.forEach((read) => read(state));
}

function goTo(path, note = "") {
  view.note = note;
  Ampify.ui.navigate(path);
}

// The gear of a card opens the page with the settings first, above the picture, where they are seen at once.
function openItem(item, note, settingsFirst = false) {
  view.item = item.key;
  view.settingsFirst = settingsFirst;
  goTo(ITEM_PAGE, note);
}

function takeNote() {
  const note = view.note;
  view.note = "";
  return note;
}

const openSettings = () => goTo(SETTINGS_PAGE);

// The state of the kit and the actions on it, for one page: what it shows, its status line and whether it waits on
// ampify. The list reads the state when it mounts; the detail page (onItemPage) shows the list's reading.
function useMarket(onItemPage) {
  const [state, setState] = useState(lastState);
  const [note] = useState(takeNote);  // what the action that opened this page said, kept over the first reading
  const [message, setMessage] = useState(note || (lastState ? "" : t("status.reading")));
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    readers.add(setState);
    return () => readers.delete(setState);
  }, []);

  // Reads the state again. A reading the user asked for puts the catalog's warning on the status line, or clears it;
  // after an action, the line keeps what the action said.
  const refresh = (reading) =>
    Ampify.kit.run(["market", "--json"]).then((result) => {
      try {
        publish(JSON.parse(result.out));
        if (reading) setMessage(lastLine(result.err));
      } catch {
        setMessage(t("status.unreadable", { error: lastLine(result.err) || t("status.unreadableReply") }));
      }
    }, (error) => setMessage(t("status.unreadable", { error: String(error) })));
  const reread = () => {
    setMessage(t("status.reading"));
    return refresh(true);
  };

  // One action is one or more ampify commands, stopped at the first failure, then the state read again. The status
  // line says what happened, or ampify's error as it is. Resolves to true when every command succeeded.
  const run = (commands, success) => {
    setBusy(true);
    setMessage(t("status.working"));
    const step = (index) => index === commands.length
      ? Promise.resolve("")
      : Ampify.kit.run(commands[index]).then((result) => result.code === 0
          ? step(index + 1)
          : lastLine(result.err) || lastLine(result.out) || t("status.failedCode", { code: result.code }));
    return step(0)
      .catch((error) => t("status.failed", { error: String(error) }))
      .then((failure) => {
        setMessage(failure || success);
        return refresh().then(() => !failure);
      })
      .finally(() => setBusy(false));
  };

  // An element without permissions is installed and put on in one click. One with permissions stays off: its detail
  // page opens, with the list of what it may do above the button that turns it on.
  const act = {
    install: (item) => {
      const theme = item.kind === "theme";
      const turnOn = theme ? ["theme", "set", item.id] : [item.kind, "enable", item.id];
      if (!item.permissions.length) {
        return run([["install", item.id, "--yes"], turnOn],
                   t(theme ? "done.installedInUse" : "done.installedOn", { name: item.name }));
      }
      const note = t(theme ? "done.installedUse" : "done.installedTurnOn", { name: item.name });
      return run([["install", item.id, "--yes"]], note).then((ok) => ok && !onItemPage && openItem(item, note));
    },
    update: (item) => run([["update", item.id, "--yes"]], t("done.updated", { name: item.name, version: item.entry.version })),
    use: (item, scheme) => run([["theme", "set", item.id].concat(scheme ? ["--scheme", scheme] : [])],
                               scheme ? t("done.inUseScheme", { name: item.name, scheme }) : t("done.inUse", { name: item.name })),
    clearTheme: () => run([["theme", "clear"]], t("done.appleLook")),
    toggle: (item, on) => run([[item.kind, on ? "enable" : "disable", item.id]], t(on ? "done.on" : "done.off", { name: item.name })),
    setting: (item, setting, label, value) => run([["settings", item.kind, item.id, setting.key, value]],
                                                  t("done.setting", { name: item.name, setting: label })),
    // Removed, the element has no detail page left: the list says so.
    remove: (item) => {
      const note = t("done.removed", { name: item.name });
      return run([["remove", item.id, "--yes"]], note).then((ok) => ok && goTo(LIST_PAGE, note));
    },
  };

  useEffect(() => {
    if (!onItemPage || !lastState) refresh(!note);
  }, []);

  // settings.txt may say "none" for no theme, as the kernel reads it.
  const activeTheme = state && state.theme && state.theme !== "none" ? state.theme : "";
  return { state, message, busy, act, reread, activeTheme };
}

function MarketItem() {
  const { state, message, busy, act, activeTheme } = useMarket(true);
  if (!state) return h(Text, null, message);
  const item = itemsOf(state).find((candidate) => candidate.key === view.item);
  if (!item) return h(Empty, { title: t("empty.title"), text: t("detail.gone") });
  return h(Details, { item, activeTheme, scheme: state.scheme, busy, act, message });
}

function Marketplace() {
  const { state, message, busy, act, reread, activeTheme } = useMarket(false);
  const [tab, setTabState] = useState(view.tab);
  const [search, setSearchState] = useState(view.search);
  const [page, setPageState] = useState(view.page);

  const setPage = (value) => {
    view.page = value;
    setPageState(value);
  };
  const setTab = (value) => {
    view.tab = value;
    setTabState(value);
    setPage(0);
  };
  const setSearch = (value) => {
    view.search = value;
    setSearchState(value);
    setPage(0);
  };

  if (!state) return h("stack", { gap: 12 }, h(Text, null, message));

  const items = itemsOf(state);
  const current = TABS.includes(tab) ? tab : TABS[0];
  const inTab = current === "installed"
    ? items.filter((item) => item.installed)
        .sort((a, b) => KIND_ORDER.indexOf(a.kind) - KIND_ORDER.indexOf(b.kind) || a.name.localeCompare(b.name))
    : items.filter((item) => item.kind === current);
  const shown = inTab.filter((item) => matches(item, search));
  const pages = Math.max(1, Math.ceil(shown.length / PAGE_SIZE));
  const shownPage = Math.min(page, pages - 1);
  const visible = shown.slice(shownPage * PAGE_SIZE, (shownPage + 1) * PAGE_SIZE);
  const catalogTab = current !== "installed";

  const empty = search.trim()
    ? h(Empty, { key: "empty", title: t("search.none", { search: search.trim() }), text: t("search.where"),
                 action: h(Button, { label: t("action.clearSearch"), align: "center", onClick: () => setSearch("") }) })
    : catalogTab && !state.catalogOk
      ? h(Empty, { key: "empty", title: t("catalog.unreadable"), text: t("catalog.unreadableText"),
                   action: h(Button, { label: t("action.refresh"), align: "center", disabled: busy, onClick: reread }) })
      : h(Empty, { key: "empty", title: t("empty.title"), text: t("empty." + current) });

  // Keyed sections: the status line coming and going does not make the kernel rebuild the cards under it.
  return h("stack", { gap: 16 },
    h("row", { key: "bar", gap: 8 },
      TABS.map((kind) => h(Button, { key: kind, label: t("tab." + kind), variant: kind === current ? "secondary" : "tertiary",
                                     onClick: () => setTab(kind) })),
      h("textfield", { key: "search", placeholder: t("search." + current), value: search, grow: true,
                       onChange: (event) => setSearch(event.value) }),
      h(Button, { key: "refresh", label: t("action.refresh"), disabled: busy, onClick: reread }),
      h(Button, { key: "settings", label: t("action.settings"), icon: SETTINGS_GLYPH, tooltip: t("action.openSettings"),
                  onClick: openSettings })),
    message ? h(Text, { key: "status", variant: "caption" }, message) : null,
    current === "theme" || current === "installed"
      ? h(ThemeBar, { key: "theme", items, activeTheme, scheme: state.scheme, busy, act })
      : null,
    catalogTab && !state.catalogOk && visible.length
      ? h(Text, { key: "offline", variant: "caption" }, t("catalog.offline"))
      : null,
    visible.length
      ? h(Grid, { key: "grid", items: visible,
                  renderCard: (item, previewHeight) => h(ItemCard, { key: item.key, item, activeTheme, busy, act, previewHeight,
                                                                     open: () => openItem(item) }) })
      : empty,
    h(Pager, { key: "pager", page: shownPage, pages, setPage }));
}

// The app's "/marketplace" page (Ampify.ready renders it), its detail page, and the "Ampify" entry of the sidebar that
// opens the list.
function render() {
  return h(Marketplace);
}
globalThis.render = render;
Ampify.ReactDOM.render(h(MarketItem), { page: ITEM_PAGE });

// Ampify's logo as a silhouette: the puzzle piece, drawn in the text color like Apple Music's own sidebar icons.
const AMPIFY_ICON =
  '<svg viewBox="0 0 64 64"><path d="M12.3 13L19 13C21.5 13 22.25 11.25 20.75 9.25C17.75 5 20.5 .75 26 .75' +
  "C31.5 .75 34.25 5 31.25 9.25C29.75 11.25 30.5 13 33 13L39.7 13A11.3 11.3 0 0 1 51 24.3L51 31C51 33.5 52.75 34.25" +
  " 54.75 32.75C59 29.75 63.25 32.5 63.25 38C63.25 43.5 59 46.25 54.75 43.25C52.75 41.75 51 42.5 51 45L51 51.7" +
  'A11.3 11.3 0 0 1 39.7 63L12.3 63A11.3 11.3 0 0 1 1 51.7L1 24.3A11.3 11.3 0 0 1 12.3 13Z"/></svg>';

Ampify.ui.title.set("Marketplace");
Ampify.ui.sidebar.addItem({ label: "Ampify", icon: AMPIFY_ICON, onClick: () => Ampify.ui.navigate(LIST_PAGE) });

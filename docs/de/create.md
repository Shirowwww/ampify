---
title: Erstellen
description: Ein Design, ein Snippet oder eine Erweiterung für Ampify bauen, live in Apple Music sehen und im Katalog teilen.
---

# Für Ampify erstellen

Ein Design ist ein Ordner mit ein paar Textdateien. Eine Erweiterung ist eine JavaScript-Datei (oder TypeScript, das für Sie kompiliert wird) und ein Manifest. Sie brauchen
Ampify, Apple Music und ein Terminal in einem Arbeitsordner. Nichts zu kompilieren, kein Konto anzulegen. Die ausführliche Dokumentation ist auf Englisch.

1. **Mit einem funktionierenden Element beginnen.**
   `ampify new theme mein-design` (oder `extension`, `snippet`, `app`) schreibt eine komplette Vorlage in den aktuellen
   Ordner. Sie läuft schon: Sie ändern etwas, das funktioniert, statt bei einer leeren Seite anzufangen.

2. **Live ansehen.**
   `ampify dev mein-design` kopiert es in Ampify, schaltet es ein und lädt es bei jedem Speichern neu. Apple Music ändert
   sich binnen einer Sekunde, ohne Neustart. Lassen Sie das Terminal offen.

3. **Herausfinden, was gestaltet werden soll.**
   Designs nutzen feste Namen für die Bereiche von Apple Music: `.sidebar`, `.playbar`, `.lcd`, `.track-row`, `.card` und über
   hundert weitere ([die vollständige Liste](../themes/names.md)). Für alles andere zeigen `ampify devtools` und danach
   `ampify inspect` den Oberflächenbaum live, mit dem Selektor jedes Elements.

4. **Formen.**
   Farben kommen in `colors.ini`, ein Abschnitt pro Farbschema. Formen, Abstände, Hover-Effekte, Übergänge und Animationen
   kommen in `user.css`, mit CSS, das Sie schon kennen. Ein Design kann eigene Einstellungen anbieten (einen Regler, einen
   Schalter, eine Auswahl), die jeder in den Einstellungen von Apple Music ändert. Siehe
   [das Design-Format](../themes/format.md).

5. **Oder Verhalten schreiben.**
   Erweiterungen nutzen die `Ampify`-API: den Player, den Titel, Benachrichtigungen, Menüs, Bereiche, Speicher und Aufrufe
   ins Web, die das Manifest angibt. Jede Berechtigung wird vor der Installation angezeigt. Siehe
   [den Erweiterungsleitfaden](../extensions/guide.md) und [die API](../extensions/api.md).

6. **Prüfen.**
   `ampify validate mein-design` prüft Manifest und Dateien nach denselben Regeln wie der Katalog.

7. **Teilen.**
   `ampify publish mein-design` prüft es noch einmal und eröffnet dann für Sie einen Pull Request im
   [öffentlichen Katalog](https://github.com/Shirowwww/ampify-catalog), ohne etwas zu klonen. Sobald er übernommen ist,
   erscheint es im Marketplace jedes Ampify und in [der Galerie](themes.html). Der ganze Weg, Vorschau inklusive, steht
   in [fünf Schritten](../extensions/catalog.md#propose-an-element-in-five-steps).
{: .steps}

## Ein Weg für jede Sache

Designs, Snippets und Erweiterungen teilen sich ihre Werkzeuge. Wenn zwei Arten von Elementen dasselbe können, tun sie es
auf dieselbe Weise: Was Sie für eine lernen, gilt auch für die anderen.

| Um... | Verwenden | Gilt für |
|---|---|---|
| Einstellungen anzubieten | Das Feld `settings` des Manifests ([Manifest](../extensions/manifest.md#settings)): CSS liest sie mit `@setting` und `--setting-<key>`, JavaScript mit `Ampify.config.get(key)` | Designs, Snippets, Erweiterungen, Apps |
| Namen, Beschreibung und Einstellungen zu übersetzen | Das Feld `translations` des Manifests | Jedes Element |
| Zu wissen, wo die Wiedergabeleiste steht | `@setting bar-position`, vom Nutzer für alle Designs gesetzt, oder das `"playbar"` des Designs | Designs, Snippets |
| Den Ecken der Bereiche zu folgen | `--panel-radius` (und `--bar-radius`, `--lcd-radius` für Leiste und Anzeige) | Designs und Snippets setzen es; Snippets und die Bereiche der Erweiterungen folgen ihm |
| Die Farben des laufenden Covers zu nutzen | `--art-1` bis `--art-3` und ihre Verwandten in CSS, `Ampify.color.playing()` in JavaScript | Alle |
| Balken zu zeichnen, die sich mit der Musik bewegen | `content: bars(n, shape, height)` | Designs, Snippets |
| Ein Element zu weichzeichnen, umzufärben oder mit Glas zu belegen | `filter` und `backdrop` | Designs, Snippets |
| Eine Zeile, einen Schalter oder ein Bedienelement nach Zustand zu gestalten | `:hover`, `:active`, `:focus`, `:current`, `:playing`, `:checked`, `:selected`, `:disabled` | Designs, Snippets |
| Einen Abschnitt der Fortschrittslinie zu markieren | `Ampify.ui.playbar.mark(id, from, to)` | Erweiterungen |
| Eine Oberfläche zu bauen | Die Knoten von `Ampify.ui`, oder `Ampify.React` mit `Ampify.components` (Tasten, Schalter, Kontrollkästchen, Regler, Felder, Menüs, Tabs, Fortschrittsbalken, Ladeanzeigen, Trenner...) | Erweiterungen, Apps |

[Das Design-Format](../themes/format.md#what-a-theme-tells-snippets-and-extensions) listet auf, was ein Design an die anderen
Elemente weitergibt, und [die API](../extensions/api.md#55-interface-stable) jeden Knoten und jede Komponente.

## Von Spicetify kommend

Eine `color.ini` von Spicetify funktioniert unverändert: In einen Design-Ordner gelegt, färben ihre Farben Apple Music ein. Die
`user.css` braucht statt der Spotify-Klassen die Namen von Ampify.
[Was übernommen wird und was nicht](../themes/spicetify.md).

## Lizenzen

Ampify steht unter der PolyForm Noncommercial License 1.0.0: frei für jede Nutzung außer der kommerziellen, mit Namensnennung.
Diese Lizenz erstreckt sich nicht auf das, was Sie erschaffen: Designs, Snippets, Erweiterungen und Apps, die nur die
dokumentierten Formate und die API von Ampify nutzen, gehören Ihnen, unter der Lizenz Ihrer Wahl, kommerziell oder nicht. Die Vorlagen von
`ampify new` und die Beispiele stehen unter der MIT-Lizenz: Kopieren Sie daraus, was Sie möchten.

## Weiter geht es

- [Ein Design in fünf Minuten](../themes/guide.md)
- [Das Design-Format: jede Eigenschaft und jeder Selektor](../themes/format.md)
- [Die festen Namen der Oberfläche von Apple Music](../themes/names.md)
- [Eine Erweiterung schreiben](../extensions/guide.md) und [ihr Manifest](../extensions/manifest.md)
- [Die Befehlszeile](../cli.md)

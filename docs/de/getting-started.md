---
title: Loslegen
description: Ampify installieren, ein Design wählen, zum ursprünglichen Apple Music zurückkehren und was zu tun ist, wenn Ampify sich nach einem Update von Apple Music ausschaltet.
---

# Loslegen mit Ampify

Ampify fügt Apple Music für Windows Designs, Erweiterungen und Apps hinzu. Es ändert nie die Dateien von Apple Music:
Wenn Sie es deinstallieren, haben Sie die ursprüngliche App zurück. Die ausführliche Dokumentation ist auf Englisch.

## Was Sie brauchen

- Windows 10 oder 11 (x64), mit Apple Music aus dem Microsoft Store, angemeldet. Ampify fragt nie nach Ihrer
  Apple-ID.
- Eine getestete Version von Apple Music. Bei einer anderen öffnet sich Apple Music ganz normal und Ampify bleibt aus (siehe
  weiter unten).

## Installieren

Beenden Sie zuerst Apple Music, auch im Infobereich neben der Uhr, wo es versteckt weiterlaufen kann.

**Mit der Zip-Datei.** Laden Sie `ampify-<version>-windows-x64.zip` von der Seite [Releases](https://github.com/Shirowwww/ampify/releases) herunter,
entpacken Sie die ganze Zip-Datei (Rechtsklick, Alle extrahieren) und doppelklicken Sie im neuen Ordner auf `ampify_setup.exe`.
Wird das Installationsprogramm direkt aus der Zip-Datei gestartet, meldet es eine fehlende Datei. Zeigt Windows „Der Computer wurde durch Windows geschützt“,
klicken Sie auf „Weitere Informationen“ und dann auf „Trotzdem ausführen“: Die Dateien sind noch nicht
signiert.

**Mit PowerShell.** Öffnen Sie es (Startmenü, „PowerShell“ eintippen) und fügen Sie ein:

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
```

Öffnen Sie danach Apple Music wie gewohnt über sein eigenes Symbol: Es gibt keine spezielle Verknüpfung.

## Ein Design wählen

Direkt nach der Installation sieht Apple Music aus wie vorher: Ampify bringt kein Design mit. Das erste kommt aus dem
Marketplace.

1. Klicken Sie unten in der Seitenleiste auf **Ampify**. Der Marketplace öffnet sich.
2. Klicken Sie unter **Designs** (Internet nötig) bei dem, das Ihnen gefällt, auf **Installieren**, zum Beispiel Glass oder
   Artwork: Es wird sofort verwendet.
3. Wählen Sie im Menü eine Variante, wenn das Design mehrere anbietet, zum Beispiel Dark oder Light.

Die Änderung ist sofort sichtbar. **Deaktivieren** (beim verwendeten Design) und **Look von Apple Music verwenden** (unten in der
Liste) bringen das ursprüngliche Aussehen zurück. Ein bereits installiertes Design funktioniert auch ohne Internet weiter.

Die Begriffe: Ein **Design** ändert das Aussehen von Apple Music, eine **Erweiterung** fügt eine Funktion hinzu, eine **App**
ist eine eigene Seite mit einem Eintrag in der Seitenleiste, ein **Snippet** sind ein paar Zeilen Stil, die zusätzlich zu einem Design
eingeschaltet werden. Im Tab **Entwickler** stehen die Beispiele für alle, die Erweiterungen bauen. Die **Variante** ist die
helle oder dunkle Fassung, die ein Design anbieten kann.

Die Position der Wiedergabeleiste gilt einmal für alle Designs: in den Einstellungen von Ampify, Tab
Darstellung, bei „Wiedergabeleiste“, unter dem Design. „Wie im Theme“ behält die Position jedes Designs; Oben, Unten
und „Unten, Fortschritt unter den Tasten“ (die Linie verläuft unter den Tasten, zwischen der abgelaufenen Zeit und der Dauer) gelten
für alle.

## Zurück zu Apple Music

Drei Wege, vom leichtesten zum gründlichsten:

1. **Nur das Design.** Im Marketplace beim verwendeten Design auf **Deaktivieren** klicken, oder auf **Look von Apple Music verwenden**.
2. **Ampify eine Weile aus**, nichts wird gelöscht. Es gibt noch keine Schaltfläche: Im Terminal schaltet `ampify restore`
   es aus, `ampify apply` wieder ein.
3. **Deinstallieren.** `ampify uninstall` im Terminal, oder Einstellungen > Apps > Installierte Apps >
   „Ampify for Apple Music“. Beide öffnen dasselbe Deinstallationsprogramm, das Apple Music schließt, falls es offen ist. Ein
   Kontrollkästchen, anfangs nicht angehakt, löscht auch Ihre Designs, Erweiterungen und Einstellungen in `%USERPROFILE%\Ampify`.
   `ampify uninstall --yes` deinstalliert ohne Fenster und behält Ihre Dateien (`--delete-data` löscht sie).

## Wenn Apple Music sich aktualisiert

Apple Music aktualisiert sich selbst über den Microsoft Store. Ampify lädt nur in Versionen, die es getestet hat, deshalb
kann es nach einem Update aus bleiben: Apple Music öffnet sich dann wie gewohnt, ohne Ihr Design, und nichts ist
kaputt. Es schaltet sich auch für einen Start aus, wenn sich Apple Music zweimal hintereinander von selbst beendet hat.

Windows zeigt dann eine Mitteilung, nur einmal pro Version: „Ampify ist vorerst aus“ (oder „Ampify hat diesen Start
übersprungen“). Steht darin, dass Ampify die neue Liste erhalten hat, beenden Sie Apple Music vollständig (auch im
Infobereich) und öffnen Sie es erneut. Sonst kommt Ampify zurück, sobald die neue Version getestet ist: Eine neue
Version von Ampify bringt das mit (Updates installieren sich selbst), und `ampify versions update` sucht die Liste
sofort. `ampify doctor` zeigt jederzeit denselben Zustand.

## Wenn Ampify sich aktualisiert

Ihre Designs, Snippets und Erweiterungen ziehen mit. Beim ersten Start einer neuen Version bringt Ampify jedes Element
aus dem Katalog auf seine neue Version und ersetzt ein entferntes Snippet durch das, das es übernommen hat, mit Ihren Einstellungen. Ohne
Internet bleiben sie, wie sie sind, und der nächste Start versucht es erneut. `ampify update --elements` macht dasselbe von
Hand.

## Etwas stimmt nicht

1. Starten Sie `ampify doctor`: Es prüft Ampify und listet jedes Problem auf.
2. Starten Sie `ampify report`: Es schreibt eine anonymisierte Zip-Datei mit Zustand und Protokollen nach `%USERPROFILE%\Ampify\reports`
   (die Schaltfläche **Bericht exportieren** auf der Einstellungsseite von Ampify tut dasselbe). Sehen Sie sie durch, dann hängen Sie sie an
   ein [Ticket](https://github.com/Shirowwww/ampify/issues) an.

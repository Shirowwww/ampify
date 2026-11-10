---
title: Per iniziare
description: Installare Ampify, scegliere un tema, tornare all'Apple Music originale, e cosa fare quando Ampify si spegne dopo un aggiornamento di Apple Music.
---

# Iniziare con Ampify

Ampify aggiunge temi, estensioni e app ad Apple Music per Windows. Non modifica mai i file di Apple Music: disinstallarlo
ti restituisce l'applicazione originale. La documentazione dettagliata è in inglese.

## Cosa serve

- Windows 10 o 11 (x64), con Apple Music installato dal Microsoft Store e con l'accesso effettuato. Ampify non chiede
  mai il tuo ID Apple.
- Una versione di Apple Music già provata. Su un'altra, Apple Music si apre normalmente e Ampify resta spento (vedi più
  avanti).

## Installare

Chiudi prima Apple Music, anche nell'area di notifica accanto all'orologio, dove può restare nascosto.

**Con lo zip.** Scarica `ampify-<version>-windows-x64.zip` dalla pagina [Releases](https://github.com/Shirowwww/ampify/releases),
estrai tutto lo zip (clic destro, Estrai tutto), poi fai doppio clic su `ampify_setup.exe` nella nuova cartella.
Se lo avvii dall'interno dello zip, il programma di installazione segnala un file mancante. Se Windows mostra «Windows
ha protetto il PC», fai clic su «Ulteriori informazioni», poi su «Esegui comunque»: i file non sono ancora firmati.

**Con PowerShell.** Aprilo (menu Start, scrivi «PowerShell») e incolla:

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
```

Poi apri Apple Music come al solito, con la sua icona: non c'è nessun collegamento speciale.

## Scegliere un tema

Subito dopo l'installazione, Apple Music è identico a prima: Ampify non include alcun tema. Il primo arriva dal
Marketplace.

1. Fai clic su **Ampify** in fondo alla barra laterale. Si apre il Marketplace.
2. In **Temi** (serve internet), fai clic su **Installa** sul tema che ti piace, per esempio Glass o
   Artwork: viene usato subito.
3. Scegli una variante dal menu se il tema ne offre più di una, per esempio Dark o Light.

Il cambiamento è immediato. **Disattiva** (sul tema in uso) e **Usa l'aspetto di Apple Music** (in fondo
all'elenco) ripristinano l'aspetto originale. Un tema già installato continua a funzionare senza internet.

Le parole usate: un **tema** cambia l'aspetto di Apple Music, un'**estensione** aggiunge una funzione, un'**app**
è una pagina a sé con una voce nella barra laterale, uno **snippet** è qualche riga di stile da attivare in
aggiunta a un tema. La scheda **Sviluppatori** raccoglie gli esempi per chi crea estensioni. La **variante** è la
versione chiara o scura che un tema può offrire.

La barra di riproduzione si sceglie una volta per tutti i temi, nelle impostazioni di Ampify, alla voce
«Barra di riproduzione». «Come previsto dal tema»
mantiene la posizione di ciascun tema; In alto, In basso e In basso, centrata valgono per tutti.

## Tornare ad Apple Music

Tre modi, dal più leggero al più completo:

1. **Solo il tema.** Nel Marketplace, **Disattiva** sul tema in uso, oppure **Usa l'aspetto di Apple Music**.
2. **Ampify spento per un po'**, senza cancellare nulla. Non c'è ancora un pulsante: in un terminale, `ampify restore`
   per spegnerlo, `ampify apply` per riaccenderlo.
3. **Disinstallare.** `ampify uninstall` in un terminale, oppure Impostazioni > App > App installate >
   «Ampify for Apple Music». Entrambi aprono lo stesso programma di disinstallazione, che chiude Apple Music se è
   aperto. Una casella, deselezionata all'inizio, elimina anche i tuoi temi, estensioni e impostazioni da
   `%USERPROFILE%\Ampify`. `ampify uninstall --yes` disinstalla senza finestre e conserva i tuoi file (`--delete-data`
   li elimina).

## Quando Apple Music si aggiorna

Apple Music si aggiorna da solo tramite il Microsoft Store. Ampify si carica solo sulle versioni che ha provato, quindi
dopo un aggiornamento può restare spento: Apple Music si apre allora come al solito, senza il tuo tema, e nulla è
rotto. Si spegne anche per un avvio quando Apple Music si è chiuso da solo due volte di fila.

Windows mostra allora una notifica, una sola volta per versione: «Ampify è spento per ora» (oppure «Ampify ha saltato
questo avvio»). Se dice che Ampify ha ricevuto il nuovo elenco, chiudi completamente Apple Music (anche nell'area di
notifica), poi riaprilo. Altrimenti, Ampify torna quando la nuova versione è stata provata: lo porta una nuova
versione di Ampify (gli aggiornamenti si installano da soli), e `ampify versions update` cerca l'elenco subito.
`ampify doctor` mostra lo stesso stato in qualsiasi momento.

## Qualcosa non va

1. Esegui `ampify doctor`: controlla Ampify ed elenca ogni problema.
2. Esegui `ampify report`: scrive uno zip anonimizzato con lo stato e i log in `%USERPROFILE%\Ampify\reports`
   (il pulsante **Esporta un rapporto** nella pagina delle impostazioni di Ampify fa la stessa cosa). Rileggilo, poi
   allegalo a una [segnalazione](https://github.com/Shirowwww/ampify/issues).

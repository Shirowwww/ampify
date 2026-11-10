---
title: Crea
description: Creare un tema, uno snippet o un'estensione per Ampify, vederlo dal vivo in Apple Music e condividerlo nel catalogo.
---

# Creare per Ampify

Un tema è una cartella con pochi file di testo. Un'estensione è un file JavaScript (o TypeScript, compilato per te) e un
manifesto. Ti servono Ampify installato, Apple Music e un terminale aperto in una cartella di lavoro. Niente da
compilare, nessun account da creare. La documentazione dettagliata è in inglese.

1. **Partire da un elemento che funziona.**
   `ampify new theme my-theme` (o `extension`, `snippet`, `app`) scrive un punto di partenza completo nella cartella
   corrente. Funziona già: modifichi qualcosa che va, invece di partire da una pagina bianca.

2. **Vederlo dal vivo.**
   `ampify dev my-theme` lo copia in Ampify, lo attiva e lo ricarica a ogni salvataggio. Apple Music cambia in meno di
   un secondo, senza riavviare. Lascia il terminale aperto.

3. **Trovare cosa modificare.**
   I temi puntano a nomi stabili per le aree di Apple Music: `.sidebar`, `.playbar`, `.lcd`, `.track-row`,
   `.card` e più di un centinaio di altri ([l'elenco completo](../themes/names.md)). Per il resto, `ampify devtools`
   poi `ampify inspect` mostrano l'albero dell'interfaccia dal vivo, con il selettore di ogni elemento.

4. **Dargli forma.**
   I colori vanno in `colors.ini`, una sezione per ogni schema di colori. Forme, spaziature, passaggi del puntatore,
   transizioni e animazioni vanno in `user.css`, con il CSS che già conosci. Un tema può dichiarare le proprie
   impostazioni (un cursore, un interruttore, una scelta), che ognuno cambia nelle impostazioni di Apple Music. Vedi
   [il formato dei temi](../themes/format.md).

5. **Oppure scrivere un comportamento.**
   Le estensioni usano l'API `Ampify`: il lettore, il brano, le notifiche, i menu, i pannelli, la
   memoria e le chiamate web che il manifesto dichiara. Ogni permesso viene mostrato prima dell'installazione. Vedi
   [la guida alle estensioni](../extensions/guide.md) e [l'API](../extensions/api.md).

6. **Verificarlo.**
   `ampify validate my-theme` controlla il manifesto e i file con le stesse regole del catalogo.

7. **Condividerlo.**
   `ampify publish my-theme` lo verifica ancora, poi apre per te una pull request sul
   [catalogo pubblico](https://github.com/Shirowwww/ampify-catalog), senza clonare nulla. Una volta accettata, compare
   nel Marketplace di ogni Ampify e nella [galleria](themes.html).
{: .steps}

## Un solo modo per ogni cosa

Temi, snippet ed estensioni condividono gli strumenti. Quando due tipi di elementi sanno fare la stessa cosa, la fanno
allo stesso modo: ciò che impari per uno serve anche per gli altri.

| Per... | Usare | Dove |
|---|---|---|
| Proporre impostazioni | Il campo `settings` del manifesto ([manifesto](../extensions/manifest.md#settings)): il CSS le legge con `@setting` e `--setting-<chiave>`, il JavaScript con `Ampify.config.get(chiave)` | Temi, snippet, estensioni, app |
| Tradurre nome, descrizione e impostazioni | Il campo `translations` del manifesto | Tutti gli elementi |
| Sapere dov'è la barra di riproduzione | `@setting bar-position`, scelto dall'utente per tutti i temi, oppure dal `"playbar"` del tema | Temi, snippet |
| Seguire gli angoli dei pannelli | `--panel-radius` (e `--bar-radius`, `--lcd-radius` per la barra e il display) | Temi e snippet lo fissano; gli snippet e i pannelli delle estensioni lo seguono |
| Prendere i colori della copertina in riproduzione | Da `--art-1` a `--art-3` e le loro varianti in CSS, `Ampify.color.playing()` in JavaScript | Tutti |
| Disegnare barre che seguono la musica | `content: bars(n, forma, altezza)` | Temi, snippet |
| Sfocare, ricolorare o applicare il vetro a un elemento | `filter` e `backdrop` | Temi, snippet |
| Stilizzare una riga, un interruttore o un controllo in base al suo stato | `:hover`, `:active`, `:focus`, `:current`, `:playing`, `:checked`, `:selected`, `:disabled` | Temi, snippet |
| Segnare un passaggio della linea di avanzamento | `Ampify.ui.playbar.mark(id, from, to)` | Estensioni |
| Costruire un'interfaccia | I nodi di `Ampify.ui`, oppure `Ampify.React` con `Ampify.components` (pulsanti, interruttori, caselle di controllo, cursori, campi, menu, schede, barre di avanzamento, indicatori di attesa, separatori...) | Estensioni, app |

[Il formato dei temi](../themes/format.md#what-a-theme-tells-snippets-and-extensions) elenca ciò che un tema trasmette
agli altri elementi, e [l'API](../extensions/api.md#55-interface-stable) ogni nodo e ogni componente.

## Se vieni da Spicetify

Un `color.ini` di Spicetify funziona così com'è: messo nella cartella di un tema, i suoi colori vestono Apple Music. Lo
`user.css` richiede i nomi di Ampify al posto delle classi di Spotify.
[Cosa passa e cosa no](../themes/spicetify.md).

## Licenze

Ampify ha licenza PolyForm Noncommercial 1.0.0: libero per ogni uso tranne quello commerciale, mantenendo il credito.
Questa licenza non si estende a ciò che crei: i temi, snippet, estensioni e app che usano solo i formati e l'API
documentati di Ampify sono tuoi, con la licenza che preferisci, commerciale o no. I punti di partenza di `ampify new`
e gli esempi hanno licenza MIT: copiane ciò che vuoi.

## Andare oltre

- [Un tema in cinque minuti](../themes/guide.md)
- [Il formato dei temi: ogni proprietà e selettore](../themes/format.md)
- [I nomi stabili dell'interfaccia di Apple Music](../themes/names.md)
- [Scrivere un'estensione](../extensions/guide.md) e [il suo manifesto](../extensions/manifest.md)
- [La riga di comando](../cli.md)

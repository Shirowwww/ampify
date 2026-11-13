# Play history

Apple Music keeps no history for a station such as Discovery: once a song has gone by, it is gone. This app keeps
one.

## What the page shows

- Every song played on this PC, newest first and grouped by day, whatever started it: an album, a playlist or a
  station.
- Its cover, title, artist, album and the time it started. A click opens the song in Apple Music.
- Thirty songs per page; the list keeps the last 1,000 plays. "Clear the history" empties it (click twice).

## How it counts

A song joins the list after 10 seconds of listening, so a song skipped at once does not. Each play is its own line:
a song heard twice shows twice. Only what plays on this PC while Ampify runs is listed; for every device, a
scrobbler such as Last.fm is the way.

## What leaves your PC

The list stays in Ampify's folder on this PC. To show a cover and open a song, the page sends the title and artist
of the lines shown (or the song's catalog id, when Apple Music gives it) to Apple: to the catalog of Apple Music's web
player, with the public token its page carries, or to the iTunes Search API when that token cannot be read. Nothing
goes to a server of Ampify.

# Discord presence

## How to use it

Switch it on with Discord open. Discord then writes "Listening to Apple Music" in the member list, with the song's
cover, title and artist.

## Settings

- **Name shown in the member list**: what follows "Listening to": the artist, the title, or the application's name.
- **Show the song's cover**: the cover from Apple's catalog, or from Deezer's for the explicit songs that Apple's
  public search leaves out; without it, the Apple Music logo.
- **Animated covers, for the few albums that have one** (off at first): an album with a motion artwork in Apple Music
  shows it moving in Discord, as Music Presence does. Discord shows no video, so Ampify makes the video a GIF on your
  PC (200 pixels, 10 frames a second, lighter for a busy video; about a second of work) and puts it on the same free
  host as the covers below, once per album. Apple gives a motion artwork to few albums (Currents, SOS or Midnights
  have one, most albums and singles do not): the others keep their still cover.
- **Put your own files' covers online**: a song neither catalog has (a file of your own) has its cover sent to a free
  host that deletes it later (x0.at keeps a cover about 100 days; Ampify sends it again after 60).
- **Show the album when the cover is hovered**: the album's name as the cover's tooltip.
- **Show the time elapsed**: Discord's progress bar.
- **Title, artist and cover open Apple Music**: each one links to its page on music.apple.com (a song found on Deezer
  only: Apple Music's search for it).
- **Stay visible while paused**: keeps the status, with a pause badge.
- **First and second button**: Apple Music, Other platforms (a song.link page with the song on Spotify, YouTube Music,
  Deezer, Tidal and others), YouTube Music, Spotify, or none.

## What leaves your PC

The title and artist go to Apple Music's catalog, once per song, to find its cover and links: through the API of
Apple's web player (music.apple.com, amp-api.music.apple.com), with the public token its page carries, as Music
Presence does; the iTunes Search API when that token cannot be read; Deezer's public search when Apple has no such
song. Nothing goes to a server of Ampify. Covers of your own files go to x0.at (else uguu.se, else
litterbox.catbox.moe) only while that setting is on. With animated covers on, the album's motion artwork is read from
Apple's video host (mvod.itunes.apple.com) and its GIF goes to the same hosts.

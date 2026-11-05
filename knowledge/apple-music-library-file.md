# Apple Music's library file

Apple Music for Windows keeps the user's library in
`<Music folder>\Apple Music\Apple Music Library.musiclibrary\Library.musicdb`, the same format as Music.app on macOS.
Apple does not document it. The layout below was checked on a real Windows library (Apple Music 1.7.0.155, 2 067
tracks, a 1.5 MB file that decodes to 6 MB); the code is `src/core/library/musicdb.cpp`. Integers are little-endian.
Apple Music rewrites the file while it runs (its size changed within minutes of listening), so its date changes often.

## Envelope

| Offset | Content |
|---|---|
| 0 | `hfma` |
| 4 | Header length (0xA0) |
| 8 | File length |
| 0x10 | Version string, `1.7.0.155` |
| 0x54 | Encrypted length of the payload (102 400) |

The payload follows the header. Its first `min(encrypted length, payload length)` bytes, rounded down to 16, are
AES-128-ECB encrypted with the key `BHUILuilfghuila3`; the rest is clear. Once decrypted, the whole payload is one zlib
stream.

## Decoded payload

A run of `hsma` sections: header length at 4 (0x38), type at 12, total length at 16. The type-1 section holds the
track list. The others hold the library header (`hfma` again, type 3), playlists, albums (`lama`), artists (`lAma`).

- `ltma`, the track list: header length at 4 (0x5C), track count at 8. The tracks follow.
- `itma`, one track: header length at 4 (0x178), total length at 8, sub-record count at 12, persistent id (UInt64) at 16,
  year (UInt16) at 0xA8 (checked on a real library in November 2026: "Timeless" gives 2024). The sub-records follow
  the header.
- `boma`, a sub-record: 0x14 at 4, total length at 8, subtype at 12.

| Subtype | Content |
|---|---|
| 0x2, 0x3, 0x4 | Title, album, artist: encoding at 20 (1 = UTF-16LE, 2 = UTF-8), byte length at 24, the text from 36 |
| 0x1 | Numbers, 384 bytes: bit rate at 108, date added at 112, date modified at 148, duration in ms at 176 |
| 0x17 | Plays, 72 bytes: persistent id again at 20, last played at 28, play count at 32, skip count at 36 |

Other subtypes: 0x5 genre, 0x6 kind, 0xB file URL (UTF-8), 0xC composer, 0x1B album artist (the three read by
Ampify, same text layout as the title), 0x1E to 0x22 sort names, 0x36 and 0x38 binary plists of cloud ids
(`cloud-album-id`, `cloud-artist-id` such as `r.agzJEin`, `cloud-universal-libray-id`: library ids, not catalog ids).

## Dates

Seconds since 1904-01-01 **in local wall-clock time**, not UTC: the most recent play of the checked library read as
22:44 in Paris (UTC+1), which was 21:44 UTC. Ampify converts them with the local time zone. 0 means never.

## Not yet understood

In the plays sub-record, 40 holds a date on 1 208 of 2 067 tracks, 200 of which have a skip count: it is not only the
last skip. 48 is another date and 52 another count (830 tracks, 1 540 in total against 9 992 plays at 32), maybe plays
from other devices. Ampify does not expose them.

Public descriptions that agree with the offsets above: the musicdb notes of AppMusicLibParser (pitetb) and the
`musicdb` crate of am-osx-status (homomorphist).

# Volume de l'Atmos : ce qu'Apple fait, ce qui marche (relevé du 2026-10-30)

Relevé fait par l'utilisateur sur son PC, Apple Music pour Windows `1.1540.23042.0`. Complète la section « Lecture » de
[apple-music-internals.md](apple-music-internals.md). Les adresses (RVA) ne servent qu'à comprendre : le kit ne
résout jamais une fonction par adresse.

## Le système d'Apple ne règle pas le volume de l'Atmos

**Sound Check ne touche jamais l'Atmos.**

- `AVCFPlayerItemSetLoudnessInfo` ne reçoit que `{"adjust to album loudness": true}`.
- Les items Atmos n'ont aucune information de loudness.
- Le tap audio (égaliseur, Sound Check) est sauté exprès pour l'Atmos : préférence
  `skip-audio-tap-for-atmos-passthrough`. Un élément qui passerait par le tap ne verrait donc pas l'Atmos.

**CoreMedia a son propre nivellement Atmos, et il ne suffit pas.**

- `media::MediaEngineWrapper::EnableLoudnessModeForDolbyAtmosMusic` (RVA `0x2067b0`) pose un drapeau (`+0x365`)
  avant la création du moteur.
- La création du moteur (`0x207130`) appelle ensuite `ApplyLoudnessModeForDolbyAtmosMusic` (`0x206cf0`). Celle-ci
  insère l'effet audio Dolby avec `CODECAPI_AVDecDDOperationalMode` = préférence `atmos-loudness-mode`
  (domaine `com.apple.coremedia`, lue une seule fois par processus, défaut `8` = PORTABLE16).
- RVA valables pour `CoreMedia.dll` dont l'horodatage PE est `0x69b9c1b1`.

**Mesures BS.1770 sur le même morceau :**

| Réglage | Loudness |
|---|---|
| Atmos, mode 8 (celui d'Apple, toujours appliqué et réussi) | -19,9 LUFS |
| Atmos, modes 7 et 6 (le décodeur retombe sans dialnorm) | -31,15 LUFS |
| Stéréo avec Sound Check | -15,9 à -16,1 LUFS |

**Ne pas retenter ce réglage.** Seul le mode 8 fonctionne, et il laisse l'Atmos environ 4 dB sous la stéréo.
Le kit ne lit ni n'écrit `atmos-loudness-mode`.

## La seule correction qui marche : un gain sur les échantillons décodés, mesuré par morceau

| Chemin | Interface | Format | Position par rapport au curseur |
|---|---|---|---|
| Stéréo | `IAudioRenderClient` | float32, 2 canaux, 96 kHz (sortie de l'utilisateur) | après : le volume du lecteur est déjà appliqué |
| Atmos | `ISpatialAudioObjectRenderStreamForMetadata` (IID `Data1` = `0xBBC9C907`) | objets mono float32, 48 kHz, 16 dynamiques | avant : le volume du curseur n'est pas encore appliqué |

Méthodes accrochées à la volée sur le flux Atmos (numéros de vtable) : `GetBuffer` en 3 (sur l'objet audio),
`BeginUpdatingAudioObjects` en 8, `EndUpdatingAudioObjects` en 9, activation d'objets en 10 et 11.

Conséquence : pour comparer l'Atmos à la stéréo, la mesure doit ramener les deux chemins à la même échelle
(la stéréo divisée par le volume du lecteur, ou l'Atmos multiplié par ce volume).

**Ne jamais passer par `AVCFPlayerSetVolume`** : le curseur d'Apple Music affiche le volume du lecteur, il bougerait.
Le noyau lit ce volume, il ne l'écrit jamais.

## Réglages du nivellement

- Cible : médiane des 30 derniers morceaux stéréo mesurés.
- Gain entre 0 et 15 dB.
- Plafond de crête 0,95, genou doux à partir de 0,94.
- Une valeur gardée par variante stable du morceau (`AVCFPlayerItemCopyCurrentStableVariantID`).

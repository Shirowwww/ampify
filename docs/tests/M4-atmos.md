# Test M4 : nivellement Atmos sur le PC

Ce test se fait sur ton PC, avec ton compte Apple Music. Il ne demande aucun mot de passe. Les journaux qu'il produit
contiennent des chemins de ton PC, dont ton nom d'utilisateur : relis-les avant de les envoyer.

Il se fait dans le même passage que le test M3, avec le même lancement. Si tu as déjà fait M3, tu peux passer
directement à l'étape 1 de celui-ci.

## Ce qu'on veut savoir

1. Le flux Atmos d'Apple Music (`ISpatialAudioObjectRenderStreamForMetadata`) est bien trouvé : le journal montre ses
   hooks posés à la volée, au premier morceau Atmos.
2. Les deux mesures tournent, chacune à son débit : la stéréo sur le flux `IAudioRenderClient` (96 kHz chez toi),
   l'Atmos sur les objets spatiaux (48 kHz).
3. Sur trois morceaux Dolby Atmos, la loudness mesurée (ligne « morceau Atmos mesuré ») est à 1 LU près de la cible
   stéréo (ligne « cible stéréo »), et le gain appliqué suit.
4. La barre de volume d'Apple Music ne bouge jamais toute seule, ni pendant la lecture, ni au changement de morceau.
5. Le son reste propre : pas de craquement, pas de coupure, pas de silence.
6. Les niveaux mesurés sont gardés d'un lancement à l'autre.
7. Le journal ne montre aucun hook posé dans CoreFP, CoreADI64 ni FairPlay.

## Ce qu'il faut

- La même version d'Apple Music que pour M3 : `1.1540.23042.0`.
- Deux morceaux stéréo (ils fixent la cible) et trois morceaux Dolby Atmos.
- Les deux fichiers de ce test, dans `C:\Tests\AMK-M4\` : `amk_launcher.exe` et `amk_kernel.dll`.

## Étape 0 : fermer Apple Music

Même chose que pour M3 : dans le Gestionnaire des tâches, il ne doit rester ni `AppleMusic.exe` ni
`AMPLibraryAgent.exe`.

## Étape 1 : activer le nivellement

Le nivellement est désactivé par défaut. Pour l'activer :

1. Ouvre `%USERPROFILE%\AppleMusicModKit\settings.txt` avec le Bloc-notes. S'il n'existe pas, crée-le.
2. Ajoute cette ligne, sans changer le reste :
   `extensions.atmos-leveler.enabled = true`
3. Enregistre.

## Étape 2 : lancer et écouter

1. Double-clique sur `C:\Tests\AMK-M4\amk_launcher.exe`.
2. Apple Music doit s'ouvrir dans les 15 secondes.
3. Note où se trouve la barre de volume. Elle doit rester à cet endroit pendant tout le test.
4. Écoute d'abord deux morceaux stéréo, au moins une minute chacun : la cible se construit sur eux.
5. Écoute ensuite le premier morceau Dolby Atmos en entier, ou au moins deux minutes. Ne le coupe pas.
6. Fais de même avec le deuxième et le troisième morceau Atmos.
7. Si la barre de volume bouge sans que tu y touches, note l'heure et le morceau qui jouait.
8. Ferme Apple Music.
9. Relance `amk_launcher.exe`, rejoue le premier morceau Atmos une dizaine de secondes, puis ferme Apple Music.

## Étape 3 : lire le journal

Ouvre `%USERPROFILE%\AppleMusicModKit\journal\AMPLibraryAgent.log`. Chaque ligne commence par l'heure, puis `Atmos :`.
On attend, dans cet ordre :

- `aucun niveau enregistré` au premier lancement, ou `niveaux relus : ...` ensuite.
- Une dizaine de lignes `hook ... posé dans ...`, puis `nivellement démarré : N hook(s) audio`.
- `mesure stéréo à 96000 Hz (IAudioRenderClient, après le volume du lecteur)` (le nombre est le débit de ta sortie).
- Pour chaque morceau stéréo : `piste stéréo, variante ...`, puis à la fin
  `morceau stéréo mesuré (flux stéréo) : X LUFS sur N s, enregistré`.
- Au premier morceau Atmos :
  - `flux spatial activé : ISpatialAudioObjectRenderStreamForMetadata (IID BBC9C907-...)`. Si la ligne finit par
    `autre que celui du SDK`, recopie-la-moi en entier ;
  - `mesure Atmos à 48000 Hz (objets spatiaux, avant le volume du curseur)` ;
  - six lignes `hook ISpatialAudioObjectRenderStreamForMetadata::...` et `hook ISpatialAudioObjectFor...::GetBuffer`,
    toutes `posé dans`.
- Pour chaque morceau Dolby Atmos : `piste Dolby Atmos, variante ..., gain de départ 5.0 dB`, puis des lignes
  `mesure en cours : X LUFS, cible stéréo Y LUFS`, puis `morceau Atmos mesuré (objets spatiaux) : X LUFS sur N s,
  enregistré`.
- Des lignes `gain A -> B dB` quand le gain change.
- Au second lancement : `niveaux relus : 3 variante(s) Atmos, 2 morceau(x) stéréo` (ou plus), puis, sur le morceau
  rejoué, un saut direct du gain (`gain 5.0 -> B dB`) dès le début, sans montée lente.

Les deux mesures tournent si tu vois à la fois `morceau stéréo mesuré (flux stéréo)` et
`morceau Atmos mesuré (objets spatiaux)`. Si un morceau Atmos dit `(flux stéréo)`, les objets n'ont rien mesuré :
envoie-moi le journal.

Pour chaque morceau Dolby Atmos, compare le X de `morceau Atmos mesuré` avec le Y de `cible stéréo`. L'écart doit être
de 1 LU au plus. Le gain qui suit (dernière ligne `gain A -> B dB` du morceau) doit valoir à peu près Y − X, entre 0
et 15 dB.

Ensuite, cherche (Ctrl + F) les mots `CoreFP`, `CoreADI` et `FairPlay`. Une ligne `posé dans` avec un de ces noms
ne doit pas exister. Une ligne `refusé` avec un de ces noms est normale : c'est le garde-fou qui fonctionne.

Si aucune ligne `piste Dolby Atmos` n'apparaît, le morceau n'a pas été reconnu comme Atmos. C'est important : note le
nom du morceau et envoie-moi le journal.

## Étape 4 : si le son est mauvais

Si le son est saccadé, coupé, ou trop fort : ferme Apple Music, remets
`extensions.atmos-leveler.enabled = false` dans `settings.txt`, relance Apple Music, et envoie-moi le journal avec
ce qui s'est passé.

## Ce qu'il faut me renvoyer

- Les fichiers `AMPLibraryAgent.log` et `AppleMusic.log` de `%USERPROFILE%\AppleMusicModKit\journal\`.
- Pour chaque morceau Dolby Atmos : la ligne `morceau Atmos mesuré`, et la ligne `cible stéréo` la plus proche.
- La barre de volume a-t-elle bougé toute seule ? Oui ou non, et quand.
- Le son était-il propre ? Oui ou non, et si non, à quel moment.
- Le journal contient-il une ligne `posé dans` avec CoreFP, CoreADI64 ou FairPlay ? Oui ou non.
- Le fichier `%USERPROFILE%\AppleMusicModKit\atmos-levels.txt` (les niveaux gardés, quelques lignes de texte).

Ce passage valide M4. Il confirme aussi la lecture audio de M3, qui reste à confirmer.

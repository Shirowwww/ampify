# Test M7 : extensions JavaScript sur le PC

But : vérifier qu'Apple Music charge une extension JavaScript du dossier utilisateur et qu'elle s'exécute. À faire une
fois, après le test M3 (`M3-injecteur.md`), avec le même lanceur.

## Ce que ce test ne vérifie pas

Les événements du lecteur, le gain sur le son, les raccourcis globaux, le réseau et les notifications Windows ne sont pas
branchés encore (M4 et M7 suite). Un exemple qui les utilise démarrera, mais n'affichera rien.

## Étapes

1. Ouvrir `%USERPROFILE%\AppleMusicModKit\` (le dossier existe après le test M3 ; sinon, le créer).
2. Copier le dossier `examples\hello-extension` dans `%USERPROFILE%\AppleMusicModKit\extensions\`. Le dossier final
   s'appelle `hello-extension`, et contient `manifest.json` et `main.js`.
3. Ouvrir (ou créer) `%USERPROFILE%\AppleMusicModKit\settings.txt`, et ajouter cette ligne :
   `extensions.hello-extension.enabled = true`
4. Lancer Apple Music avec le lanceur, comme dans le test M3.
5. Ouvrir `%USERPROFILE%\AppleMusicModKit\journal\AppleMusic.log` et chercher :
   - `Éléments JavaScript retenus : 1` ;
   - `Extension hello-extension démarrée` ;
   - `[hello-extension] Bonjour, lancement n° 1`.
   Le journal `AMPLibraryAgent.log` ne doit contenir aucune ligne de `hello-extension` : l'exemple déclare `"processes": ["ui"]`.
6. Ouvrir `%USERPROFILE%\AppleMusicModKit\storage\hello-extension.json`. Il doit contenir `{"launches":1}`
   (ou un chiffre plus grand, si Apple Music a déjà été lancé avec le lanceur).
7. Fermer Apple Music, le relancer, et vérifier que le compteur augmente de exactement 1 à chaque lancement.

## Ce qu'il faut me renvoyer

- Les lignes de `AppleMusic.log` qui contiennent `hello-extension` ou `Éléments JavaScript`.
- Le contenu de `storage\hello-extension.json` après chaque lancement.
- Si une ligne est absente : le journal complet `AppleMusic.log`, sans rien retirer.

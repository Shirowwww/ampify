# Test M5 et M6 : kit de test, installation et désinstallation

Ce test se fait sur ton PC, avec ton compte Apple Music. Il regroupe deux étapes pour que tu n'aies à le faire qu'une fois :

- **M5** : le kit de test s'installe, lance Apple Music, enregistre une trace pendant que tu l'utilises, et écrit un rapport.
- **M6** : tu désinstalles le kit depuis les Paramètres, et Apple Music redevient normal.

Compte environ 10 minutes. Tu peux refuser n'importe quelle étape : le test dira ce qui manque.

## Ce qu'il faut

- Un PC Windows 11 avec Apple Music pour Windows installé, et ton compte.
- Les fichiers du dossier `AMK-M5-M6` (le zip que je t'ai envoyé). Mets-les tous ensemble dans un dossier, par exemple `C:\Tests\AMK-M5-M6\` :
  - `AMK-Test.exe` : le kit de test, celui sur lequel tu cliques.
  - `amk_setup.exe` : l'installateur, que le kit utilise tout seul.
  - `amk_launcher.exe` et `amk_kernel.dll` : le lanceur et le noyau.

## Étape 0 : fermer Apple Music

Ferme Apple Music complètement. Dans le Gestionnaire des tâches (Ctrl + Maj + Échap), vérifie qu'il ne reste ni `AppleMusic.exe` ni `AMPLibraryAgent.exe`.

## Étape 1 : double-clic sur AMK-Test.exe (M5)

1. Double-clique sur `AMK-Test.exe`.
2. Une fenêtre demande si on continue. Clique **Oui**.
3. Windows peut afficher une demande d'autorisation (contrôle de compte) pour la trace. Clique **Oui** : la trace sert à voir ce que fait l'app. Si tu préfères ne pas la donner, clique **Non** : le test continue sans trace. Il peut y en avoir une deuxième demande à la fin : même choix.
4. Apple Music s'ouvre tout seul, par le raccourci « Apple Music avec mods » du menu Démarrer.

## Étape 2 : utiliser Apple Music pendant 2 minutes

Une fenêtre s'affiche par-dessus Apple Music. Pendant environ 2 minutes :

1. cherche un album et lance un titre stéréo ;
2. lance un titre Dolby Atmos si tu en as un ;
3. mets en pause, passe au titre suivant, ouvre les Réglages.

Quand c'est fait, clique **OK** dans la fenêtre. Ne ferme pas Apple Music avant.

## Étape 3 : lire le résultat

Une fenêtre annonce « Test terminé : RÉUSSI », « À REVOIR » ou « NON EXÉCUTÉ ». Un dossier s'ouvre dans l'Explorateur :
`%USERPROFILE%\AppleMusicModKit\rapports\`. Il contient un fichier `.zip` avec la date du test dans son nom.

Dans le même dossier `rapports\`, ouvre le dossier au nom de la date (le zip en porte le même nom) et ouvre `rapport.txt` : chaque ligne commence par `[OK]`, `[NON]`, `[?]` ou `[i]`.

- `[OK]` : c'est bon.
- `[NON]` : ça n'a pas marché. Note la ligne.
- `[?]` : à regarder. Par exemple « 0 thread repris » : rien n'était suspendu au lancement.

Si la ligne « Apple Music trouvé » dit « introuvable », Apple Music n'est pas installé pour ton compte : dis-le-moi, le reste du test ne compte pas.

## Étape 4 : désinstaller le kit (M6)

1. Ouvre **Paramètres > Applications > Applications installées** (ou tape « Applications installées » dans la recherche Windows).
2. Cherche **Apple Music avec Mod Kit**, puis clique sur les trois points, puis **Désinstaller**.
3. Une fenêtre demande de confirmer. Clique **Oui**.
4. Une autre fenêtre demande si on garde tes réglages, tes journaux et tes rapports. Clique **Oui** : ils restent dans `%USERPROFILE%\AppleMusicModKit\`, et tu peux les envoyer. Clique **Non** si tu veux tout effacer.
5. Une fenêtre annonce la fin de la désinstallation.

Ensuite, vérifie :

- le raccourci « Apple Music avec mods » n'est plus dans le menu Démarrer ;
- le dossier `%LOCALAPPDATA%\Programs\AppleMusicModKit` n'existe plus.

## Étape 5 : Apple Music sans mods (M6)

1. Lance Apple Music depuis le menu Démarrer, par son icône habituelle (pas par un raccourci du kit).
2. Utilise-le 30 secondes. Ferme-le.
3. Ne relance rien d'autre. Apple Music doit démarrer et se fermer comme avant, sans fenêtre d'erreur.

## Ce qu'il faut me renvoyer

1. Le fichier `.zip` du dossier `%USERPROFILE%\AppleMusicModKit\rapports\`. Ton nom d'utilisateur est remplacé par « utilisateur » dans les chemins, sauf dans `trace.etl` (fichier binaire, non anonymisé).
2. Réponse à ces questions, en une ligne chacune :
   - Le résultat du test (RÉUSSI, À REVOIR ou NON EXÉCUTÉ), et les lignes `[NON]` s'il y en a.
   - Apple Music s'est-il ouvert, et était-il utilisable pendant les 2 minutes ?
   - Dans l'étape 4, la désinstallation a-t-elle fini sans erreur ?
   - Dans l'étape 5, Apple Music a-t-il démarré normalement après la désinstallation ?

## En cas de problème

- **Apple Music ne s'ouvre pas** : ferme-le depuis le Gestionnaire des tâches, puis envoie le zip quand même. C'est le cas le plus utile.
- **Une fenêtre d'erreur du kit** : note le texte exact.
- **Antivirus ou Windows Defender bloque un fichier** : note le nom de la menace. N'autorise rien sans me le dire.
- **La désinstallation dit qu'Apple Music est ouvert** : ferme Apple Music, attends 10 secondes, relance la désinstallation.

# Test M3 : lanceur et noyau sur le PC

Ce test se fait sur ton PC, avec ton compte Apple Music. Il ne demande aucun mot de passe. Les journaux qu'il produit
contiennent des chemins de ton PC, dont ton nom d'utilisateur : relis-les avant de les envoyer.

## Ce qu'on veut savoir

1. Le lanceur ouvre Apple Music, et l'app reste utilisable.
2. Le noyau se charge dans `AppleMusic.exe` et dans `AMPLibraryAgent.exe`. Le journal le confirme.
3. Windows lance l'app en pause (« suspendu ») avant l'injection. Le journal dit si ça a marché.
4. Sans le lanceur, Apple Music démarre comme d'habitude, sans noyau.

## Ce qu'il faut

- Un PC Windows 11 avec Apple Music pour Windows installé.
- La version d'Apple Music doit être `1.1540.23042.0`. Le lanceur la lit lui-même et l'écrit dans
  `launcher.log` (ligne « Paquet trouvé »). Si le numéro est différent, le lanceur refuse de charger le noyau et
  lance l'app normalement. C'est le comportement prévu : signale-le-moi.
- Les deux fichiers livrés avec ce test, dans le même dossier : `amk_launcher.exe` et `amk_kernel.dll`.
  Mets-les dans `C:\Tests\AMK-M3\`.

## Étape 0 : fermer Apple Music

Ferme Apple Music complètement. Dans le Gestionnaire des tâches (Ctrl + Maj + Échap), vérifie qu'il ne reste ni
`AppleMusic.exe` ni `AMPLibraryAgent.exe`. S'il en reste un, termine-le.

## Étape 1 : lancer avec le lanceur

1. Double-clique sur `C:\Tests\AMK-M3\amk_launcher.exe`.
2. Apple Music doit s'ouvrir dans les 15 secondes. Aucune fenêtre d'erreur ne doit apparaître. Si une fenêtre
   d'erreur apparaît, note son texte et passe à l'étape 4.
3. Pendant 2 minutes, utilise l'app : cherche un album, lance un titre stéréo, puis un titre Dolby Atmos si tu en
   as un, mets en pause, passe au titre suivant, ouvre les Réglages.
4. Ferme Apple Music.

## Étape 2 : lire les journaux

Ouvre le dossier `%USERPROFILE%\AppleMusicModKit\journal\` (tape ce chemin dans la barre de l'Explorateur).
Il contient trois fichiers : `launcher.log`, `AppleMusic.log` et `AMPLibraryAgent.log`.

Dans `launcher.log`, on attend ces lignes, dans cet ordre (les heures et les numéros de processus varient) :

- `Paquet trouvé : AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa ...`
- `version 1.1540.23042.0 testée : noyau injecté`
- `Lancement suspendu demandé à Windows`, ou une ligne « Lancement suspendu indisponible (0x...) ». Les deux sont
  possibles. Dans le second cas, le noyau est injecté après le démarrage, ce qui est moins sûr. Il faut le noter.
- `Apple Music démarré (processus N)`
- `AppleMusic.exe : noyau chargé`
- `N thread(s) repris après l'injection`. Ce chiffre est important : `0` veut dire que rien n'était suspendu.
- `AMPLibraryAgent.exe (M) : noyau chargé`
- `Apple Music fermé : le lanceur s'arrête`

Dans `AppleMusic.log` : `Noyau chargé dans AppleMusic.exe`, puis `version 1.1540.23042.0 testée`, puis
`Hôte d'extensions prêt`. Environ 20 secondes après le démarrage : `Application saine après 20 s`.

Dans `AMPLibraryAgent.log` : les mêmes trois premières lignes, avec `AMPLibraryAgent.exe` à la place.

Si un fichier manque, ou si une ligne attendue n'apparaît pas, ce n'est pas grave : copie le contenu du fichier tel
quel et envoie-le-moi.

## Étape 3 : vérifier qu'on peut revenir au lancement normal

1. Lance Apple Music depuis le menu Démarrer, comme d'habitude (sans le lanceur).
2. Utilise-la 30 secondes, puis ferme-la.
3. Relis `AppleMusic.log` : sa date et son contenu doivent être ceux de l'étape 1. Aucune nouvelle ligne ne doit
   s'être ajoutée. Un lancement normal ne crée pas de journal.

## Étape 4 : mode sans échec (facultatif, à faire une fois)

Le mode sans échec désactive le noyau après deux lancements qui n'ont pas tenu 20 secondes. Pour le tester sans
faire planter l'app :

1. Ferme Apple Music.
2. Ouvre `%USERPROFILE%\AppleMusicModKit\state.txt` avec le Bloc-notes. Remplace la ligne
   `unhealthy_launches = ...` par `unhealthy_launches = 2`. Si la ligne n'existe pas, ajoute-la. Enregistre.
3. Double-clique sur `amk_launcher.exe`.
4. Dans `launcher.log`, on attend `mode sans échec actif : Apple Music démarre sans noyau`. Apple Music doit
   s'ouvrir normalement.
5. Attends 25 secondes sans fermer l'app. `launcher.log` doit afficher
   `Application saine sans noyau après 20 s`, et `state.txt` doit revenir à `unhealthy_launches = 0`.

## En cas de problème

- **Apple Music reste figé au démarrage** : ferme-le depuis le Gestionnaire des tâches. Envoie les trois journaux.
  C'est le cas le plus important à signaler.
- **Une fenêtre d'erreur du lanceur** : note le texte exact.
- **Antivirus ou Windows Defender bloque le lanceur** : note le nom de la menace et ce qui a été bloqué.
  N'autorise rien sans me le dire.
- **Le lanceur ne fait rien** : vérifie que `amk_kernel.dll` est dans le même dossier que `amk_launcher.exe`.
  Le journal `launcher.log` doit le dire.

## Ce qu'il faut me renvoyer

Les trois journaux de `%USERPROFILE%\AppleMusicModKit\journal\`, et ta réponse à ces trois questions :

1. Apple Music s'est-il ouvert, et était-il utilisable pendant les 2 minutes ?
2. La ligne « N thread(s) repris » indiquait-elle `0` ou un nombre supérieur à 0 ?
3. Le lancement normal (étape 3) a-t-il fonctionné sans journal nouveau ?

Ces tests suffisent pour valider M3. Les autres tests de la même étape seront regroupés avec eux.

# Test M8 : thèmes sur ton PC

Ce test se fait sur ton PC, avec ton compte Apple Music. Il ne demande ni mot de passe ni code. Les journaux
contiennent des chemins de ton PC, dont ton nom d'utilisateur : relis-les avant de les envoyer.

**Pour l'instant, ce test ne peut pas encore tourner.** La liaison avec l'interface (`win/xaml_backend.cpp`) n'est
pas compilée : elle demande les en-têtes Windows App SDK, qui ne sont pas dans l'environnement de travail. Le
banc Windows doit d'abord la compiler. Cette procédure sert de plan : elle sera complétée quand le banc aura produit
la DLL avec la liaison XAML. Les étapes 1 et 2 ne dépendent pas de cette DLL et peuvent être faites dès maintenant.

## Ce qu'on veut savoir

1. Quelles ressources (brosses) l'app expose, et quels éléments elle contient sur la page Général. Sans cette liste,
   on ne peut pas écrire de règles fiables.
2. Si un thème de test change bien une couleur, sans redémarrage, puis la rend d'origine quand on choisit « Aucun ».
3. Si « Thème » apparaît dans la page Général des Réglages.
4. Si le mode `StaticResource` ou `ThemeResource` change le comportement d'une ressource modifiée à l'exécution.

## Étape 1 : vérifier les thèmes d'exemple (sans Apple Music)

1. Ouvre une invite de commandes dans le dossier où tu as mis `amk_theme_check.exe`.
2. Lance `amk_theme_check.exe` sur chacun des dossiers `themes\examples\*`.
3. Chaque dossier doit finir par « Résultat : utilisable » et sans ligne « Problème ». Si ce n'est pas le cas,
   copie-moi la sortie complète.

## Étape 2 : copier un thème dans ton dossier de réglages

1. Ouvre `%USERPROFILE%\AppleMusicModKit\` (tape ce chemin dans la barre de l'Explorateur). Si le dossier n'existe
   pas, lance une fois `amk_launcher.exe`, puis ferme Apple Music.
2. Crée un dossier `themes` à l'intérieur, puis copie dedans le dossier `minuit` (et seulement lui).

## Étape 3 : relever les noms (inspecteur)

1. Ouvre `%USERPROFILE%\AppleMusicModKit\settings.txt` avec le Bloc-notes. Ajoute, ou modifie, ces lignes :

   ```
   theme.inspect = 1
   ```

2. Lance Apple Music avec `amk_launcher.exe`. Ouvre les Réglages, puis la page Général. Reste-y 10 secondes.
3. Ferme Apple Music. Ouvre `%USERPROFILE%\AppleMusicModKit\journal\inspector.txt`.
4. Envoie-moi ce fichier. Le réglage `theme.inspect` repasse seul à `0`.

## Étape 4 : appliquer le thème de test

1. Ajoute dans `settings.txt` :

   ```
   theme.active = minuit
   theme.scheme = Dark
   ```

2. Lance Apple Music avec `amk_launcher.exe`. Dans la page Général, le choix « Thème » doit afficher `minuit`
   (pas encore vérifié : si la section n'apparaît pas, note-le, c'est une information utile).
3. Le fond des pages doit devenir sombre, et le texte clair. Note ce que tu vois.

## Étape 5 : revenir en arrière

1. Dans la page Général, choisis « Aucun ». Sans redémarrage, les couleurs doivent revenir.
2. Si ce n'est pas possible, mets `theme.active = none` dans `settings.txt`, redémarre l'app, et note-le.

## Étape 6 : lire le journal

Ouvre `%USERPROFILE%\AppleMusicModKit\journal\AppleMusic.log`. Cherche ces lignes :

- `Thèmes : fil d'interface prêt (DispatcherQueue de la fenêtre principale)` : la liaison fonctionne (la première étape du test).
- `Thème « minuit » : N valeur(s) appliquée(s)` : N donne le nombre de valeurs écrites.
- `Refusé : …` et `Ressource absente…` : les noms qui ne correspondent pas à l'app. Envoie-moi toutes ces lignes.
- `Thème retiré : …` : la restauration.

## Ce que tu me renvoies

- `inspector.txt` (étape 3), après relecture.
- Le fichier `AppleMusic.log` complet, après relecture.
- Une phrase sur ce que tu as vu à l'étape 4 et à l'étape 5.

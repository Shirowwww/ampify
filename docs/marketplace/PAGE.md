# Page « Marketplace » dans l'app

Spécification de la deuxième phase de la marketplace (PARITE-SPICETIFY.md, section 6). La première phase est faite :
`amk market`, `amk install`, `amk update` et `amk remove` lisent le même catalogue et passent par les mêmes contrôles
(voir [COMMANDES.md](../cli/COMMANDES.md), « Installer »). Cette page ne fait qu'afficher et déclencher ces
opérations. Elle ne contourne aucun contrôle.

Dépendances : le moteur de thèmes et d'interface XAML (fil XAML, M8), et les pages d'app (`ui.page`, décrit dans
[API.md](../extensions/API.md), section 5.5). Tant que `ui.page` n'existe pas, la page n'est pas livrée et
`amk market` reste la seule entrée.

## Objectif

Parcourir, installer, mettre à jour et retirer des extensions, thèmes, apps et snippets, sans quitter Apple Music
et sans ligne de commande. Une personne qui ne sait pas ce qu'est un terminal doit pouvoir installer un thème en
trois clics, et voir avant chaque installation ce que l'élément peut faire.

## Emplacement

Une entrée « Kit » dans la barre latérale d'Apple Music, ou une section dans les Réglages d'Apple Music (décision
à prendre avec l'utilisateur, voir QUESTIONS.md). La page s'appelle « Marketplace ». Elle est une page d'app
(`AMK.ui.page.register`), donc elle ne tourne que quand on l'ouvre.

## Données

- Source : le même catalogue que la CLI (`catalog.url` dans les réglages, par défaut
  `catalog/catalog.json` du dépôt). Le dernier catalogue lu est gardé dans `cache\catalog.json`.
- Hors ligne ou catalogue distant indisponible : la page affiche la copie en cache et l'indique en haut.
- Catalogue invalide : la page refuse de l'afficher et montre les raisons, comme `amk validate`. Elle ne garde pas
  une liste partielle.
- Le catalogue est lu une seule fois par ouverture de page. Un bouton « Actualiser » le relit.

## Écran

1. **Liste.** Une ligne par élément : nom, auteur, type (extension, thème, app, snippet), version, description sur
   une ligne, et un état : « non installé », « installé », « mise à jour disponible », « activé », « invalide ».
   Recherche par texte, filtre par type. Tri par nom, puis par date de mise à jour du catalogue.
2. **Fiche.** Nom, identifiant, auteur (lien vers le profil GitHub), version, licence, page du projet (`homepage`),
   description complète. Les **permissions** sont listées avec une phrase en français pour chacune
   (par exemple `network:api.example.com` : « envoie des requêtes à api.example.com »). Une permission inconnue
   du kit ne peut pas figurer dans le catalogue, donc il n'y a pas de cas à traiter.
3. **Actions** selon l'état :
   - non installé : « Installer » ;
   - installé, mise à jour disponible : « Mettre à jour » (affiche les permissions nouvelles avant de confirmer) ;
   - installé : « Activer » ou « Désactiver », « Retirer » ;
   - thème : « Choisir ce thème » et le choix du schéma.
   Les actions destructives (« Retirer ») demandent une confirmation. Une sauvegarde est faite avant, comme en CLI.

## Règles

- **Rien ne s'installe sans un clic dans la page**, et les permissions sont affichées avant ce clic.
- **Le même moteur que `amk install`.** Téléchargement en https, SHA-256 vérifié avant toute extraction, contrôle
  de la liste des fichiers, refus des liens symboliques, manifeste comparé au catalogue. La page ne réimplémente
  rien : elle appelle le moteur du kit.
- **Un élément installé est désactivé.** Son activation est un second clic, sur la fiche, qui affiche de nouveau
  ses permissions.
- **Aucun élément n'est exécuté pendant l'installation.** Le code ne tourne qu'après activation, et seulement avec
  les permissions que l'utilisateur a vues.
- **Une mise à jour reprend les réglages.** Un élément activé reste activé, sauf si sa nouvelle version demande une
  permission nouvelle que l'utilisateur refuse.
- **Pas de réseau hors du catalogue et des releases.** La page ne charge aucune image ni script depuis une autre
  adresse. Une icône vient de l'archive, pas d'Internet.

## États et messages

| Situation | Affichage |
|---|---|
| Chargement du catalogue | Indicateur, pas de liste vide |
| Catalogue vide | « Le catalogue est vide pour l'instant. » |
| Pas de réseau, cache présent | Liste du cache, bandeau « Hors ligne : dernière lecture le … » |
| Pas de réseau, pas de cache | « Impossible de lire le catalogue. Réessaie quand tu es connecté. » |
| Empreinte SHA-256 différente | « Le fichier téléchargé ne correspond pas à celui du catalogue. Rien n'a été installé. » |
| Archive refusée (chemin dangereux) | « Cette archive contient un fichier hors de son dossier. Rien n'a été installé. » |
| Version d'API non prise en charge | Élément affiché, action « Installer » désactivée, raison écrite |
| Apple Music non testé avec cette version | Bandeau sur la fiche : « Non testé avec cette version d'Apple Music. Le noyau reste inactif. » |

Les textes sont traduits avec `AMK.locale` (français et anglais, fichiers séparés).

## Accessibilité

Navigation complète au clavier, nom accessible sur chaque bouton, états annoncés (« installé », « désactivé »), et
contraste suffisant en mode clair et sombre (les couleurs viennent des ressources du thème par défaut du kit).

## Critères de réussite

1. Installer, activer, désactiver et retirer une extension depuis la page, sans terminal. Le résultat est le même
   que `amk install`, `amk extension enable`, `amk extension disable` et `amk remove`.
2. Un catalogue avec une archive modifiée d'un octet est refusé, avec le message de la table, et rien n'est écrit.
3. Les permissions affichées sur la fiche sont exactement celles de `amk extension enable`.
4. La page se ferme et se rouvre sans relire le réseau si le cache est récent.
5. Les tests : la logique de la page (filtres, états, messages) se teste dans `tools/test_examples.mjs` avec un
   faux `AMK` ; l'affichage se vérifie sur le PC de l'utilisateur, par le kit de test.

## Questions ouvertes

- Emplacement : barre latérale du kit, ou section des Réglages d'Apple Music.
- Le catalogue reste dans ce dépôt (adresse raw GitHub), ou il déménage dans un dépôt dédié (CATALOG.md le prévoit).
- Une icône par élément : format SVG seulement, ou aussi une image PNG (plus lourde, plus de cas à contrôler).

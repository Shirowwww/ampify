# Catalogue communautaire

Le catalogue est la liste publique des extensions, thèmes, apps et snippets installables. Il est aujourd'hui dans
[catalog/catalog.json](../../catalog/catalog.json), vide. Le schéma est dans
[schemas/catalog.schema.json](../../schemas/catalog.schema.json). Le format des éléments est dans [MANIFEST.md](MANIFEST.md).

Il pourra un jour déménager dans un dépôt séparé. Le format restera le même.

## Format

```json
{
  "catalogVersion": 1,
  "extensions": [
    {
      "id": "hello-extension",
      "name": "Bonjour",
      "version": "0.1.0",
      "kind": "extension",
      "author": "Shirowwww",
      "description": "Exemple minimal : écrit dans le journal et garde un compteur de lancements.",
      "kitApi": "1.0",
      "url": "https://example.com/releases/hello-extension-0.1.0.zip",
      "sha256": "0000000000000000000000000000000000000000000000000000000000000000",
      "permissions": ["storage"]
    }
  ]
}
```

L'exemple ci-dessus n'est pas dans le catalogue : son SHA-256 est factice.

| Champ | Obligatoire | Règle |
|---|---|---|
| `id` | oui | Unique dans le catalogue. Même motif que dans le manifeste. |
| `name`, `version`, `kind`, `author`, `description`, `kitApi` | oui | Mêmes règles que dans le manifeste. Le catalogue en reprend les valeurs. |
| `url` | oui | Archive `.zip` en `https://`. Son contenu est le dossier de l'élément, avec son `manifest.json` à la racine. |
| `sha256` | oui | Empreinte SHA-256 de l'archive, en minuscules, 64 caractères. |
| `homepage`, `license` | non | Mêmes règles que dans le manifeste. |
| `permissions` | non | Copie des permissions du manifeste. Elle sert à afficher le consentement avant le téléchargement. |

## Publier un élément

1. Préparer le dossier de l'élément et vérifier son manifeste :
   `python3 tools/validate.py manifest <dossier>/manifest.json`.
2. Créer l'archive `.zip` du dossier, sans le dossier parent, et publier l'archive en `https://` (par exemple dans une
   release GitHub).
3. Calculer son empreinte : `sha256sum <archive>.zip` (ou `certutil -hashfile <archive>.zip SHA256` sous Windows).
4. Ajouter l'entrée au catalogue, dans une demande de modification (pull request).
5. Valider le catalogue : `python3 tools/validate.py catalog catalog/catalog.json`.
6. Relecture humaine du code avant fusion. C'est la principale protection contre un élément malveillant.

Une mise à jour est une nouvelle entrée avec une nouvelle `version`, une nouvelle `url` et un nouveau `sha256`. Elle
passe par la même relecture. Le retrait d'un élément se fait aussi par une demande de modification.

## Confiance

Le bac à sable (voir [API.md](API.md)) limite ce qu'un élément peut faire aux permissions qu'il a déclarées. Il ne
rend pas l'élément sûr pour autant : une permission accordée est une permission réelle. Le catalogue suit donc ces règles :

- ne liste que du code relu par un mainteneur ;
- fige chaque archive par son SHA-256. Une archive modifiée après coup est refusée à l'installation ;
- affiche les permissions avant l'installation ;
- ne met pas à jour un élément sans l'accord de l'utilisateur ;
- ne donne aucun accès au code natif. Les extensions natives restent intégrées au noyau (voir [API.md](API.md), section 2).

## Tests

```bash
python3 -m unittest discover -s tools -v
```

Les tests couvrent le catalogue du dépôt, les entrées valides et invalides, les manifestes, et les mots-clés du schéma
que le validateur refuse.

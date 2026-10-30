# Format du manifeste

Chaque extension, thème, app ou snippet est un dossier qui contient un fichier `manifest.json` à sa racine. Le nom du
dossier est l'identifiant de l'élément. Le schéma est dans [schemas/manifest.schema.json](../../schemas/manifest.schema.json).
L'API que ces fichiers utilisent est décrite dans [API.md](API.md).

## Exemple

Le dossier [examples/hello-extension](../../examples/hello-extension/) est un exemple complet :

```json
{
  "$schema": "../../schemas/manifest.schema.json",
  "id": "hello-extension",
  "name": "Bonjour",
  "version": "0.1.0",
  "kind": "extension",
  "author": "Shirowwww",
  "description": "Exemple minimal : écrit dans le journal et garde un compteur de lancements.",
  "kitApi": "1.0",
  "main": "main.js",
  "permissions": ["storage"]
}
```

## Champs communs

| Champ | Obligatoire | Règle | Rôle |
|---|---|---|---|
| `id` | oui | `[a-z0-9][a-z0-9-]{1,39}`, égal au nom du dossier | Identifiant stable. Il sert aux clés de réglages. |
| `name` | oui | 1 à 60 caractères | Nom affiché dans l'interface |
| `version` | oui | `MAJEUR.MINEUR.CORRECTIF`, par exemple `1.2.0` | Version de l'élément, pas de l'API |
| `kind` | oui | `extension`, `theme`, `app` ou `snippet` | Ce que l'élément apporte |
| `author` | oui | Identifiant GitHub | Auteur affiché dans le catalogue |
| `description` | oui | 1 à 300 caractères | Résumé affiché dans le catalogue |
| `kitApi` | oui | `MAJEUR.MINEUR`, par exemple `1.0` | Version de l'API visée. La majeure doit être prise en charge. |
| `homepage` | non | URL `https://` | Page du projet |
| `license` | non | 1 à 64 caractères | Licence de l'auteur |
| `appVersions` | non | Liste de versions d'Apple Music, 16 au plus | Versions où l'élément a été testé. Le noyau applique aussi sa propre liste. |
| `permissions` | non | Liste de 32 au plus, valeurs de la section 4 de [API.md](API.md) | Ce dont l'élément a besoin. Affiché avant l'activation. |
| `processes` | non | Une ou deux valeurs : `ui` (AppleMusic.exe), `engine` (AMPLibraryAgent.exe), sans doublon | Où l'élément tourne. Par défaut, `["ui"]` : l'interface seulement, une seule fois par lancement. Un élément qui agit sur le son déclare `engine`. Avec les deux, il tourne deux fois et doit éviter d'écrire le même stockage. |

## Champs par `kind`

| Champ | Extension | App | Thème | Snippet | Règle |
|---|---|---|---|---|---|
| `main` | requis | requis | facultatif | interdit | Script `.js` à la racine du dossier |
| `type` | facultatif | facultatif | facultatif | — | `script` (par défaut) ou `module` : `main` est alors un module ES, qui peut importer d'autres fichiers du dossier (API.md, section 3.6) |
| `icon` | — | facultatif | — | — | Icône `.svg` de la barre latérale |
| `colors` | — | — | requis, ou `resources` | — | Fichier `.ini` des schémas de couleurs |
| `defaultScheme` | — | — | facultatif | — | Section de `colors.ini` utilisée par défaut |
| `resources` | — | — | requis, ou `colors` | requis | Dictionnaire de ressources `.xaml` |

Les champs qui ne concernent pas le `kind` sont ignorés par le noyau. Le validateur ne vérifie que les champs requis
pour chaque `kind`.

Les fichiers déclarés dans le manifeste sont à la racine du dossier. Un sous-dossier, un chemin relatif comme
`../x.js`, ou un nom qui commence par un point, est refusé dans ces champs. Le dossier peut contenir des sous-dossiers
pour les fichiers que `main` importe (`import x from "./lib/x.js"`). Un lien symbolique, où qu'il soit dans le dossier,
est refusé.

## Ce que vérifie le validateur

[tools/validate.py](../../tools/validate.py) contrôle :

1. le schéma : types, motifs, champs inconnus refusés ;
2. le nom du fichier, qui doit être `manifest.json` ;
3. `id`, égal au nom du dossier ;
4. la version majeure de `kitApi` ;
5. les champs requis par `kind` (tableau ci-dessus) ;
6. la présence, dans le dossier, de chaque fichier déclaré ;
7. l'absence de lien symbolique dans le dossier (le noyau refuse d'importer à travers un lien).

`amk validate` applique les mêmes règles (`cli/element.cpp`), et le noyau les refait au chargement
(`core/manifest.cpp`).

Le noyau devra refaire ces contrôles au démarrage (jalon M7), sans s'appuyer sur le validateur. Les permissions inconnues
seront refusées par le schéma comme par le noyau.

```bash
python3 tools/validate.py manifest examples/hello-extension/manifest.json
```

## Évolution du format

- `kitApi` est la version du format. Une évolution incompatible augmente la majeure.
- Un champ ajouté dans une version mineure est facultatif. Un manifeste valide l'ancienne version reste valide.
- Un champ retiré passe d'abord en « déprécié » dans une version mineure, avec un avertissement dans le journal.

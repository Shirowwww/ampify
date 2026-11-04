---
title: Créer
description: Faire un thème, un snippet ou une extension pour Ampify, le voir en direct dans Apple Music et le partager dans le catalogue.
---

# Créer pour Ampify

Un thème, c'est un dossier de quelques fichiers texte. Une extension, un fichier JavaScript (ou TypeScript, compilé pour vous) et un manifeste. Il vous
faut Ampify installé, Apple Music et un terminal ouvert dans un dossier de travail. Rien à compiler, aucun compte à
créer. La documentation détaillée est en anglais.

1. **Partir d'un élément qui marche.**
   `ampify new theme mon-theme` (ou `extension`, `snippet`, `app`) écrit un point de départ complet dans le dossier
   courant. Il tourne déjà : vous modifiez quelque chose qui marche au lieu de partir d'une page blanche.

2. **Le voir en direct.**
   `ampify dev mon-theme` le copie dans Ampify, l'active et le recharge à chaque enregistrement. Apple Music change en
   moins d'une seconde, sans redémarrer. Laissez le terminal ouvert.

3. **Trouver quoi styler.**
   Les thèmes visent des noms stables pour les zones d'Apple Music : `.sidebar`, `.playbar`, `.lcd`, `.track-row`,
   `.card` et plus d'une centaine d'autres ([la liste complète](../themes/names.md)). Pour le reste, `ampify devtools`
   puis `ampify inspect` montrent l'arbre de l'interface en direct, avec le sélecteur de chaque élément.

4. **Lui donner forme.**
   Les couleurs vont dans `colors.ini`, une section par schéma de couleurs. Formes, espacements, survols, transitions
   et animations vont dans `user.css`, avec le CSS que vous connaissez. Un thème peut déclarer ses propres réglages (un
   curseur, un interrupteur, un choix), que chacun change dans les réglages d'Apple Music. Voir
   [le format des thèmes](../themes/format.md).

5. **Ou écrire un comportement.**
   Les extensions utilisent l'API `Ampify` : le lecteur, le morceau, les notifications, les menus, les panneaux, le
   stockage et les appels web que le manifeste déclare. Chaque permission est montrée avant l'installation. Voir
   [le guide des extensions](../extensions/guide.md) et [l'API](../extensions/api.md).

6. **Le vérifier.**
   `ampify validate mon-theme` contrôle le manifeste et les fichiers avec les mêmes règles que le catalogue.

7. **Le partager.**
   `ampify publish mon-theme` le vérifie encore, puis ouvre pour vous une pull request sur le
   [catalogue public](https://github.com/Shirowwww/ampify-catalog), sans rien cloner. Une fois acceptée, elle apparaît
   dans le Marketplace de chaque Ampify et dans [la galerie](themes.html).
{: .steps}

## En venant de Spicetify

Un `color.ini` de Spicetify marche tel quel : posé dans le dossier d'un thème, ses couleurs habillent Apple Music. Le
`user.css` demande les noms d'Ampify à la place des classes de Spotify.
[Ce qui passe, et ce qui ne passe pas](../themes/spicetify.md).

## Licences

Ampify est sous licence PolyForm Noncommercial 1.0.0 : libre pour tout usage sauf commercial, en gardant le crédit.
Cette licence ne s'étend pas à ce que vous créez : les thèmes, snippets, extensions et apps qui n'utilisent que les
formats et l'API documentés d'Ampify sont à vous, sous la licence de votre choix, commerciale ou non. Les points de
départ de `ampify new` et les exemples sont sous licence MIT : copiez-en ce que vous voulez.

## Aller plus loin

- [Un thème en cinq minutes](../themes/guide.md)
- [Le format des thèmes : chaque propriété et sélecteur](../themes/format.md)
- [Les noms stables de l'interface d'Apple Music](../themes/names.md)
- [Écrire une extension](../extensions/guide.md) et [son manifeste](../extensions/manifest.md)
- [La ligne de commande](../cli.md)

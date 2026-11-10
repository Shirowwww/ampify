---
title: Débuter
description: Installer Ampify, choisir un thème, revenir à l'Apple Music d'origine, et que faire quand Ampify s'éteint après une mise à jour d'Apple Music.
---

# Débuter avec Ampify

Ampify ajoute des thèmes, des extensions et des apps à Apple Music pour Windows. Il ne modifie jamais les fichiers
d'Apple Music : le désinstaller vous rend l'application d'origine. La documentation détaillée est en anglais.

## Ce qu'il faut

- Windows 10 ou 11 (x64), avec Apple Music installé depuis le Microsoft Store et connecté. Ampify ne demande jamais
  votre identifiant Apple.
- Une version d'Apple Music testée. Sur une autre, Apple Music s'ouvre normalement et Ampify reste éteint (voir plus
  bas).

## Installer

Fermez d'abord Apple Music, y compris dans la zone de notification à côté de l'horloge, où il peut rester caché.

**Avec le zip.** Téléchargez `ampify-<version>-windows-x64.zip` sur la page [Releases](https://github.com/Shirowwww/ampify/releases),
extrayez tout le zip (clic droit, Extraire tout), puis double-cliquez sur `ampify_setup.exe` dans le nouveau dossier.
Lancé depuis l'intérieur du zip, l'installateur signale un fichier manquant. Si Windows affiche « Windows a protégé
votre ordinateur », cliquez sur « Informations complémentaires », puis « Exécuter quand même » : les fichiers ne sont pas
encore signés.

**Avec PowerShell.** Ouvrez-le (menu Démarrer, tapez « PowerShell ») et collez :

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
```

Ensuite, ouvrez Apple Music comme d'habitude, avec sa propre icône : il n'y a pas de raccourci spécial.

## Choisir un thème

Juste après l'installation, Apple Music est identique à avant : Ampify ne livre aucun thème. Le premier vient du
Marketplace.

1. Cliquez sur **Ampify** en bas de la barre latérale. Le Marketplace s'ouvre.
2. Sous **Thèmes** (il faut internet), cliquez sur **Installer** pour celui qui vous plaît, par exemple Glass ou
   Artwork : il est utilisé tout de suite.
3. Choisissez une variante dans le menu si le thème en propose plusieurs, par exemple Dark ou Light.

Le changement est immédiat. **Désactiver** (sur le thème utilisé) et **Revenir au look d'Apple Music** (en bas de la
liste) rendent l'apparence d'origine. Un thème déjà installé continue de marcher sans internet.

Les mots employés : un **thème** change l'apparence d'Apple Music, une **extension** ajoute une fonction, une **app**
est une page à part avec son entrée dans la barre latérale, un **snippet** est quelques lignes de style à activer en
plus d'un thème. L'onglet **Développeurs** réunit les exemples pour qui crée des extensions. La **variante** est la
version claire ou sombre qu'un thème peut proposer.

La barre de lecture se place une fois pour tous les thèmes, dans les réglages d'Ampify, onglet
Fenêtre, à « Barre de lecture ». « Selon le thème »
garde la place de chaque thème ; En haut, En bas et En bas, centrée valent pour tous.

## Revenir à Apple Music

Trois façons, de la plus légère à la plus complète :

1. **Seulement le thème.** Dans le Marketplace, **Désactiver** sur le thème utilisé, ou **Revenir au look d'Apple Music**.
2. **Ampify éteint un moment**, rien de supprimé. Il n'y a pas encore de bouton : dans un terminal, `ampify restore`
   pour l'éteindre, `ampify apply` pour le rallumer.
3. **Désinstaller.** `ampify uninstall` dans un terminal, ou Paramètres > Applications > Applications installées >
   « Ampify for Apple Music ». Les deux ouvrent le même désinstallateur, qui ferme Apple Music s'il est ouvert. Une
   case, décochée au départ, supprime aussi vos thèmes, extensions et réglages de `%USERPROFILE%\Ampify`.
   `ampify uninstall --yes` désinstalle sans fenêtre et garde vos fichiers (`--delete-data` les supprime).

## Quand Apple Music se met à jour

Apple Music se met à jour tout seul par le Microsoft Store. Ampify ne se charge que sur les versions qu'il a testées, donc
après une mise à jour il peut rester éteint : Apple Music s'ouvre alors comme d'habitude, sans votre thème, et rien n'est
cassé. Il s'éteint aussi pour un démarrage quand Apple Music s'est fermé de lui-même deux fois de suite.

Windows affiche alors une notification, une seule fois par version : « Ampify est coupé pour le moment » (ou « Ampify
a sauté ce démarrage »). Si elle dit qu'Ampify a reçu la nouvelle liste, fermez complètement Apple Music (aussi dans la
zone de notification), puis rouvrez-le. Sinon, Ampify revient quand la nouvelle version est testée : une nouvelle
version d'Ampify l'apporte (les mises à jour s'installent seules), et `ampify versions update` cherche la liste tout de
suite. `ampify doctor` donne le même état à tout moment.

## Quand Ampify se met à jour

Vos thèmes, snippets et extensions suivent. Au premier démarrage d'une nouvelle version, Ampify passe chaque élément
du catalogue à sa nouvelle version, et remplace un snippet retiré par celui qui l'a repris, avec vos réglages. Sans
internet, ils restent tels quels et le démarrage suivant réessaie. `ampify update --elements` fait la même chose à la
main.

## Quelque chose ne va pas

1. Lancez `ampify doctor` : il vérifie Ampify et liste chaque problème.
2. Lancez `ampify report` : il écrit un zip anonymisé de l'état et des journaux dans `%USERPROFILE%\Ampify\reports`
   (le bouton **Exporter un rapport** de la page de réglages d'Ampify fait la même chose). Relisez-le, puis joignez-le à
   un [ticket](https://github.com/Shirowwww/ampify/issues).

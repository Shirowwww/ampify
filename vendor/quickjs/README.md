# QuickJS (vendored)

Moteur JavaScript embarqué pour les extensions (`core/js_*`) : [quickjs-ng](https://github.com/quickjs-ng/quickjs)
v0.17.0, la branche maintenue de QuickJS, qui se compile officiellement avec MSVC, MinGW, GCC et Clang. Licence MIT,
voir [LICENSE](LICENSE).

Seuls les fichiers de la bibliothèque sont copiés : `quickjs.c`, `dtoa.c`, `libregexp.c`, `libunicode.c` et les
en-têtes qu'ils incluent. Ni `quickjs-libc` (modules `std` et `os`), ni `qjs`, ni les tests ne sont repris. Les
fichiers ne sont pas modifiés : [VERSION](VERSION) donne le commit du tag et l'empreinte SHA-256 de chaque fichier.

Pour mettre à jour : copier les mêmes fichiers depuis le nouveau tag (plus les nouveaux en-têtes que `quickjs.c`
inclut), reprendre la liste des sources, les définitions et les options MSVC de la cible `qjs` du `CMakeLists.txt`
amont dans la cible `amk_quickjs`, relancer `scripts/check-all.sh`, puis mettre à jour `VERSION`.

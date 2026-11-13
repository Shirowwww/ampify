---
title: Crear
description: Haz un tema, un snippet o una extensión para Ampify, velo en directo en Apple Music y compártelo en el catálogo.
---

# Crear para Ampify

Un tema es una carpeta con unos pocos archivos de texto. Una extensión es un archivo JavaScript (o TypeScript, que se compila por ti) y un manifiesto. Necesitas
Ampify instalado, Apple Music y una terminal abierta en una carpeta de trabajo. Nada que compilar, ninguna cuenta que
crear. La documentación detallada está en inglés.

1. **Empieza con un elemento que funcione.**
   `ampify new theme mi-tema` (o `extension`, `snippet`, `app`) escribe una plantilla completa en la carpeta actual.
   Ya funciona: cambias algo que anda en vez de empezar con la página en blanco.

2. **Velo en directo.**
   `ampify dev mi-tema` lo copia en Ampify, lo activa y lo recarga cada vez que guardas. Apple Music cambia en menos de
   un segundo, sin reiniciar. Deja la terminal abierta.

3. **Encuentra qué estilizar.**
   Los temas usan nombres estables para las zonas de Apple Music: `.sidebar`, `.playbar`, `.lcd`, `.track-row`, `.card` y más de
   un centenar de otros ([la lista completa](../themes/names.md)). Para lo demás, `ampify devtools` y luego `ampify inspect`
   muestran el árbol de la interfaz en directo, con el selector de cada elemento.

4. **Dale forma.**
   Los colores van en `colors.ini`, una sección por esquema de color. Formas, espacios, efectos al pasar el puntero,
   transiciones y animaciones van en `user.css`, con el CSS que ya conoces. Un tema puede declarar sus propios ajustes (un
   control deslizante, un interruptor, una opción) que cada persona cambia en los ajustes de Apple Music. Mira
   [el formato de los temas](../themes/format.md).

5. **O escribe un comportamiento.**
   Las extensiones usan la API `Ampify`: el reproductor, la canción, las notificaciones, los menús, los paneles, el
   almacenamiento y las llamadas a la web que declara el manifiesto. Cada permiso se muestra antes de instalar. Mira
   [la guía de extensiones](../extensions/guide.md) y [la API](../extensions/api.md).

6. **Compruébalo.**
   `ampify validate mi-tema` revisa el manifiesto y los archivos con las mismas reglas que el catálogo.

7. **Compártelo.**
   `ampify publish mi-tema` lo revisa otra vez y abre por ti una pull request en el
   [catálogo público](https://github.com/Shirowwww/ampify-catalog), sin clonar nada. Cuando se acepta,
   aparece en el Marketplace de cada Ampify y en [la galería](themes.html). El camino completo, vista previa incluida,
   cabe en [cinco pasos](../extensions/catalog.md#propose-an-element-in-five-steps).
{: .steps}

## Una sola manera para cada cosa

Temas, snippets y extensiones comparten herramientas. Cuando dos tipos de elemento pueden hacer lo mismo, lo hacen de la
misma manera: lo que aprendes con uno sirve para los otros.

| Para... | Usa | Funciona en |
|---|---|---|
| Ofrecer ajustes | El campo `settings` del manifiesto ([manifiesto](../extensions/manifest.md#settings)): el CSS los lee con `@setting` y `--setting-<key>`, el JavaScript con `Ampify.config.get(key)` | Temas, snippets, extensiones, apps |
| Traducir el nombre, la descripción y los ajustes | El campo `translations` del manifiesto | Todos los elementos |
| Saber dónde está la barra de reproducción | `@setting bar-position`, elegido por el usuario para todos los temas, o el `"playbar"` del tema | Temas, snippets |
| Seguir las esquinas de los paneles | `--panel-radius` (y `--bar-radius`, `--lcd-radius` para la barra y la pantallita) | Los temas y snippets lo fijan; los snippets y los paneles de las extensiones lo siguen |
| Usar los colores de la portada que suena | `--art-1` a `--art-3` y sus parientes en CSS, `Ampify.color.playing()` en JavaScript | Todos |
| Dibujar barras que se mueven con la música | `content: bars(n, shape, height)` | Temas, snippets |
| Desenfocar, recolorear o poner cristal en un elemento | `filter` y `backdrop` | Temas, snippets |
| Estilizar una fila, un interruptor o un control según su estado | `:hover`, `:active`, `:focus`, `:current`, `:playing`, `:checked`, `:selected`, `:disabled` | Temas, snippets |
| Marcar un tramo de la línea de progreso | `Ampify.ui.playbar.mark(id, from, to)` | Extensiones |
| Construir una interfaz | Los nodos de `Ampify.ui`, o `Ampify.React` con `Ampify.components` (botones, interruptores, casillas, controles deslizantes, campos, menús, pestañas, barras de progreso, indicadores de carga, separadores...) | Extensiones, apps |

[El formato de los temas](../themes/format.md#what-a-theme-tells-snippets-and-extensions) enumera lo que un tema pasa a los demás
elementos, y [la API](../extensions/api.md#55-interface-stable) cada nodo y cada componente.

## Si vienes de Spicetify

Un `color.ini` de Spicetify funciona tal cual: puesto en la carpeta de un tema, sus colores visten Apple Music. El
`user.css` necesita los nombres de Ampify en lugar de las clases de Spotify.
[Qué pasa y qué no](../themes/spicetify.md).

## Licencias

Ampify tiene la licencia PolyForm Noncommercial 1.0.0: libre para cualquier uso salvo el comercial, conservando el crédito.
Esa licencia no alcanza a lo que creas: los temas, snippets, extensiones y apps que solo usan los formatos y la API
documentados de Ampify son tuyos, con la licencia que elijas, comercial o no. Las plantillas de `ampify new` y los
ejemplos tienen licencia MIT: copia de ellos lo que quieras.

## Ir más lejos

- [Un tema en cinco minutos](../themes/guide.md)
- [El formato de los temas: cada propiedad y selector](../themes/format.md)
- [Los nombres estables de la interfaz de Apple Music](../themes/names.md)
- [Escribir una extensión](../extensions/guide.md) y [su manifiesto](../extensions/manifest.md)
- [La línea de comandos](../cli.md)

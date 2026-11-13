---
title: Primeros pasos
description: Instalar Ampify, elegir un tema, volver al Apple Music original y qué hacer cuando Ampify se apaga tras una actualización de Apple Music.
---

# Primeros pasos con Ampify

Ampify añade temas, extensiones y apps a Apple Music para Windows. Nunca modifica los archivos de Apple Music:
si lo desinstalas, recuperas la aplicación original. La documentación detallada está en inglés.

## Qué necesitas

- Windows 10 u 11 (x64), con Apple Music instalado desde la Microsoft Store y con la sesión iniciada. Ampify nunca pide
  tu ID de Apple.
- Una versión de Apple Music comprobada. Con otra, Apple Music se abre con normalidad y Ampify se queda apagado (mira
  más abajo).

## Instalar

Cierra primero Apple Music, también desde el área de notificaciones junto al reloj, donde puede seguir oculto.

**Con el zip.** Descarga `ampify-<version>-windows-x64.zip` de la página [Releases](https://github.com/Shirowwww/ampify/releases),
extrae todo el zip (clic derecho, Extraer todo) y haz doble clic en `ampify_setup.exe` dentro de la carpeta nueva.
Si lo ejecutas desde dentro del zip, el instalador avisa de que falta un archivo. Si Windows muestra «Windows protegió tu PC»,
pulsa «Más información» y luego «Ejecutar de todos modos»: los archivos todavía no están
firmados.

**Con PowerShell.** Ábrelo (menú Inicio, escribe «PowerShell») y pega:

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
```

Después abre Apple Music como siempre, con su propio icono: no hay ningún acceso directo especial.

## Elegir un tema

Justo después de instalar, Apple Music se ve igual que antes: Ampify no trae ningún tema. El primero viene del
Marketplace.

1. Haz clic en **Ampify**, abajo en la barra lateral. Se abre el Marketplace.
2. En **Temas** (hace falta internet), pulsa **Instalar** en el que te guste, por ejemplo Glass o
   Artwork: se usa al instante.
3. Elige una variante en el menú si el tema ofrece varias, por ejemplo Dark o Light.

El cambio es inmediato. **Desactivar** (en el tema en uso) y **Usar el aspecto de Apple Music** (al final de la
lista) devuelven el aspecto original. Un tema ya instalado sigue funcionando sin internet.

Las palabras que se usan: un **tema** cambia el aspecto de Apple Music, una **extensión** añade una función, una **app**
es una página aparte con su entrada en la barra lateral y un **snippet** son unas pocas líneas de estilo que se activan además de
un tema. La pestaña **Desarrolladores** reúne los ejemplos para quien crea extensiones. La **variante** es la versión
clara u oscura que puede ofrecer un tema.

La barra de reproducción se coloca una sola vez para todos los temas, en los ajustes de Ampify, pestaña
Aspecto, en «Barra de reproducción», bajo el tema. «Según el tema» respeta el sitio de cada tema; Arriba, Abajo
y «Abajo, progreso bajo los botones» (la línea pasa bajo los botones, entre el tiempo transcurrido y la duración) valen
para todos.

## Volver a Apple Music

Tres maneras, de la más ligera a la más completa:

1. **Solo el tema.** En el Marketplace, **Desactivar** en el tema en uso, o **Usar el aspecto de Apple Music**.
2. **Ampify apagado un rato**, sin borrar nada. Todavía no hay botón: en una terminal, `ampify restore`
   lo apaga y `ampify apply` lo vuelve a encender.
3. **Desinstalar.** `ampify uninstall` en una terminal, o Configuración > Aplicaciones > Aplicaciones instaladas >
   «Ampify for Apple Music». Los dos abren el mismo desinstalador, que cierra Apple Music si está abierto. Una
   casilla, sin marcar al principio, borra también tus temas, extensiones y ajustes de `%USERPROFILE%\Ampify`.
   `ampify uninstall --yes` desinstala sin ventana y conserva tus archivos (`--delete-data` los borra).

## Cuando Apple Music se actualiza

Apple Music se actualiza solo a través de la Microsoft Store. Ampify solo se carga en las versiones que ha comprobado, así que
tras una actualización puede quedarse apagado: Apple Music se abre entonces como siempre, sin tu tema, y no se ha
roto nada. También se apaga durante un inicio cuando Apple Music se ha cerrado solo dos veces seguidas.

Windows muestra entonces una notificación, una sola vez por versión: «Ampify está apagado por ahora» (o «Ampify se saltó
este inicio»). Si dice que Ampify ha recibido la nueva lista, cierra Apple Music del todo (también desde el
área de notificaciones) y ábrelo de nuevo. Si no, Ampify vuelve cuando se comprueba la nueva versión: una nueva
versión de Ampify lo trae (las actualizaciones se instalan solas), y `ampify versions update` busca la lista al
momento. `ampify doctor` muestra el mismo estado en cualquier momento.

## Cuando Ampify se actualiza

Tus temas, snippets y extensiones lo siguen. En el primer inicio de una versión nueva, Ampify lleva cada elemento
del catálogo a su versión nueva y sustituye un snippet retirado por el que lo ha recogido, con tus ajustes. Sin
internet, se quedan como estaban y el siguiente inicio lo intenta de nuevo. `ampify update --elements` hace lo mismo a
mano.

## Algo no va bien

1. Ejecuta `ampify doctor`: revisa Ampify y enumera cada problema.
2. Ejecuta `ampify report`: escribe un zip anonimizado con el estado y los registros en `%USERPROFILE%\Ampify\reports`
   (el botón **Exportar un informe** de la página de ajustes de Ampify hace lo mismo). Échale un vistazo y adjúntalo a
   una [incidencia](https://github.com/Shirowwww/ampify/issues).

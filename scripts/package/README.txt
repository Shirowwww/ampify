Ampify @VERSION@
================

Themes, extensions and apps for Apple Music on Windows. Ampify never changes Apple Music itself: the usual Apple Music
icon still starts it without Ampify, and uninstalling puts everything back as it was.

First, unzip the whole folder (right-click the zip, "Extract All"). The files do not work when started from inside
the zip.

1. Close Apple Music.
2. Run ampify_setup.exe. It installs Ampify, puts the Marketplace in your Ampify folder and adds
   "Apple Music with Ampify" to the Start menu.
3. Start menu > "Apple Music with Ampify".
4. Click "Ampify" at the bottom of Apple Music's sidebar: the Marketplace, where you pick themes and extensions.

Windows SmartScreen or Microsoft Defender may warn about ampify_setup.exe or ampify_launcher.exe. The files are not
code-signed yet, and the launcher loads Ampify into Apple Music, as some malware loads code into other programs. It is
a false positive: https://github.com/Shirowwww/ampify#install

The files:

  ampify_setup.exe                         installs Ampify, and uninstalls it from Windows Settings
  ampify_launcher.exe, ampify_kernel.dll   the launcher and the kernel, copied by the installer
  ampify.exe                               the command line tool (ampify help)
  examples\                                the Marketplace and Now Playing, example themes and extensions
  SHA256SUMS.txt                           hashes of the files, to check that they are intact
  BUILD_INFO.txt                           where this version comes from

If something goes wrong:

- The usual Apple Music icon always starts Apple Music without Ampify.
- If Apple Music crashes or freezes twice in a row at startup with Ampify, Ampify turns itself off at the next
  launch (safe mode). Apple Music then opens normally.
- To remove everything: Windows Settings > Apps > Installed apps > "Ampify for Apple Music" > Uninstall.
- The logs are in %USERPROFILE%\Ampify\logs\.

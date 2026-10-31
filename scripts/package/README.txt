Ampify @VERSION@ (test version)
=============================================

This folder holds Ampify, which adds themes, extensions and apps to Apple Music for Windows.
It never changes Apple Music itself. Uninstalling puts everything back as it was.

First, unzip the whole folder (right-click the zip, "Extract All").
The files do not work when started from inside the zip.

In this order:

1. ampify_setup.exe          installs Ampify. It adds "Apple Music with Ampify" to the Start menu.
2. prepare-test.cmd         copies the example extensions and themes, and turns on the ones used by the test.
3. Start menu > "Apple Music with Ampify": Apple Music opens with Ampify.
4. Ampify-Test.exe          runs the full test and writes a report to send back.

The step-by-step guide, with what to look at and what to send back, is in TEST-GUIDE.txt.

The other files:

  ampify_launcher.exe, ampify_kernel.dll   the launcher and the kernel, copied by the installer
  ampify.exe                               the command line tool (ampify help)
  examples\                                example extensions and themes
  SHA256SUMS.txt                           hashes of the files, to check that they are intact
  BUILD_INFO.txt                           where this version comes from
  TEST-GUIDE.txt                           the test guide, step by step

If something goes wrong:

- The usual Apple Music icon always starts Apple Music without Ampify.
- If Apple Music crashes or freezes twice in a row at startup with Ampify, Ampify turns itself off at the next
  launch (safe mode). Apple Music then opens normally.
- To remove everything: Windows Settings > Apps > Installed apps > "Ampify for Apple Music"
  > Uninstall.
- The logs are in %USERPROFILE%\Ampify\logs\.

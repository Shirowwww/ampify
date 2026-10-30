@echo off
rem Prepares the V0.5 test pass: copies the example extensions and themes next to this file into the user's kit
rem folder, then enables the ones the guide uses. Run it again at any time: it overwrites the examples, nothing else.
setlocal
rem UTF-8, so the French messages below show their accents. "if errorlevel" is used rather than "||", which some
rem cmd implementations (Wine's) evaluate from a stale error level.
chcp 65001 >nul
set "HERE=%~dp0"
set "KIT=%USERPROFILE%\AppleMusicModKit"

if not exist "%HERE%amk.exe" goto :notextracted
if not exist "%HERE%exemples\extensions" goto :notextracted

echo Préparation du test : copie des exemples dans %KIT%
xcopy "%HERE%exemples\extensions" "%KIT%\extensions\" /e /i /y /q >nul
if errorlevel 1 goto :failed
xcopy "%HERE%exemples\themes" "%KIT%\themes\" /e /i /y /q >nul
if errorlevel 1 goto :failed

echo.
echo Activation du nivellement Atmos et des extensions du test
"%HERE%amk.exe" extension enable atmos-leveler
if errorlevel 1 goto :failed
"%HERE%amk.exe" extension enable react-panel
if errorlevel 1 goto :failed
"%HERE%amk.exe" extension enable now-playing-notify
if errorlevel 1 goto :failed
"%HERE%amk.exe" extension enable hello-extension
if errorlevel 1 goto :failed
rem The theme stays off: the guide has it chosen in Réglages > Général. Its dark scheme is the one to see.
"%HERE%amk.exe" config theme.scheme Dark
if errorlevel 1 goto :failed

echo.
"%HERE%amk.exe" list
echo.
echo C'est prêt. Lance « Apple Music avec mods » depuis le menu Démarrer.
pause
exit /b 0

:notextracted
echo Ce fichier doit rester à côté de amk.exe et du dossier « exemples ».
echo Dézippe d'abord le dossier entier (clic droit sur le zip, « Extraire tout »), puis relance ce fichier.
pause
exit /b 1

:failed
echo.
echo La préparation n'a pas abouti. Fais une capture de cette fenêtre et envoie-la.
pause
exit /b 1

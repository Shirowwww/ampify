@echo off
rem Prepares the test pass: copies the example extensions and themes next to this file into the user's Ampify
rem folder, then enables the ones the guide uses. Run it again at any time: it overwrites the examples, nothing else.
setlocal
rem UTF-8, so names with accents print correctly. "if errorlevel" is used rather than "||", which some
rem cmd implementations (Wine's) evaluate from a stale error level.
chcp 65001 >nul
set "HERE=%~dp0"
set "KIT=%USERPROFILE%\Ampify"

if not exist "%HERE%ampify.exe" goto :notextracted
if not exist "%HERE%examples\extensions" goto :notextracted

echo Preparing the test: copying the examples to %KIT%
xcopy "%HERE%examples\extensions" "%KIT%\extensions\" /e /i /y /q >nul
if errorlevel 1 goto :failed
xcopy "%HERE%examples\themes" "%KIT%\themes\" /e /i /y /q >nul
if errorlevel 1 goto :failed
xcopy "%HERE%examples\apps" "%KIT%\apps\" /e /i /y /q >nul
if errorlevel 1 goto :failed

echo.
echo Turning on the Atmos leveler and the test extensions
"%HERE%ampify.exe" extension enable atmos-leveler
if errorlevel 1 goto :failed
"%HERE%ampify.exe" extension enable react-panel
if errorlevel 1 goto :failed
"%HERE%ampify.exe" extension enable now-playing-notify
if errorlevel 1 goto :failed
"%HERE%ampify.exe" extension enable hello-extension
if errorlevel 1 goto :failed
"%HERE%ampify.exe" extension enable now-playing
if errorlevel 1 goto :failed
"%HERE%ampify.exe" app enable marketplace
if errorlevel 1 goto :failed
rem The theme stays off: the guide has it chosen in Settings > General. Its dark scheme is the one to see.
"%HERE%ampify.exe" config theme.scheme Dark
if errorlevel 1 goto :failed

echo.
"%HERE%ampify.exe" list
echo.
echo All set. Start "Apple Music with Ampify" from the Start menu.
pause
exit /b 0

:notextracted
echo This file must stay next to ampify.exe and the "examples" folder.
echo Unzip the whole folder first (right-click the zip, "Extract All"), then run this file again.
pause
exit /b 1

:failed
echo.
echo The preparation did not finish. Take a screenshot of this window and send it back.
pause
exit /b 1

@echo off
rem ------------------------------------------------------------------
rem  Crea l'installer di RegiScope con un doppio clic.
rem  1) compila RegiScope in Release (x64) con il CMake di Visual Studio
rem  2) impacchetta tutto con Inno Setup
rem  Risultato: installer\output\RegiScope-Setup-<versione>.exe
rem ------------------------------------------------------------------
setlocal
cd /d "%~dp0"

rem --- trova Visual Studio (serve il carico "Sviluppo di applicazioni desktop con C++")
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [ERRORE] Visual Studio non trovato.
  goto :errore
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo [ERRORE] In Visual Studio manca il carico "Sviluppo di applicazioni desktop con C++".
  goto :errore
)

rem --- CMake: quello nel PATH, altrimenti quello incluso in Visual Studio
set "CMAKE=cmake"
where cmake >nul 2>nul
if errorlevel 1 set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" if not "%CMAKE%"=="cmake" (
  echo [ERRORE] CMake non trovato. In Visual Studio Installer aggiungi "Strumenti CMake C++ per Windows".
  goto :errore
)

rem --- trova Inno Setup 6 (installazione per tutti o per utente)
set "ISCC="
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe"      set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC (
  echo [ERRORE] Inno Setup 6 non trovato.
  echo Scaricalo da https://jrsoftware.org/isdl.php
  echo oppure da un prompt:  winget install JRSoftware.InnoSetup
  goto :errore
)

echo.
echo === 1/2  Compilo RegiScope in Release ===
"%CMAKE%" -S .. -B ..\build-installer -A x64
if errorlevel 1 goto :errore
"%CMAKE%" --build ..\build-installer --config Release
if errorlevel 1 goto :errore

echo.
echo === 2/2  Creo l'installer ===
"%ISCC%" /DBuildDir=..\build-installer\Release RegiScope.iss
if errorlevel 1 goto :errore

echo.
echo Fatto! L'installer e' in: %~dp0output
explorer "%~dp0output"
pause
exit /b 0

:errore
echo.
echo Creazione dell'installer non riuscita.
pause
exit /b 1

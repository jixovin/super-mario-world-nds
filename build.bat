@echo off
rem Double-click to build Super Mario World DS with your installed devkitPro.
rem Output: nds\smw-nds.nds  (copy it to your R4 microSD card)
rem
rem The first build needs the game data made from YOUR OWN ROM. Put your
rem Super Mario World USA ROM at roms\smw.sfc, and have Python 3 installed.
rem The data stays on your PC and is never uploaded to GitHub.
setlocal
set "ROOT=%~dp0"
set "ASSETS=%ROOT%nds\data\smw_assets.bin"
set "ASSETS_VER=%ROOT%nds\data\assets_version.txt"
rem Bump this when the asset tool changes, so old game data is re-made.
set "WANT_VER=2"
set "MSYS_BASH=C:\devkitPro\msys2\usr\bin\bash.exe"

if not exist "%MSYS_BASH%" (
  echo devkitPro was not found at C:\devkitPro. Install the NDS Development package first.
  goto :fail
)

set "HAVE_VER="
if exist "%ASSETS_VER%" set /p HAVE_VER=<"%ASSETS_VER%"
if not exist "%ASSETS%" set "HAVE_VER="
if not "%HAVE_VER%"=="%WANT_VER%" (
  call :make_assets
  if errorlevel 1 goto :fail
  >"%ASSETS_VER%" echo %WANT_VER%
)

"%MSYS_BASH%" -lc "cd \"$(cygpath -u '%ROOT%nds')\" && make"
if errorlevel 1 goto :fail
echo.
echo BUILD OK: %ROOT%nds\smw-nds.nds
pause
exit /b 0

:make_assets
set "ROM=%ROOT%roms\smw.sfc"
if not exist "%ROM%" (
  echo Game data is missing. Copy your Super Mario World USA ROM to:
  echo   %ROM%
  exit /b 1
)
set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY where python >nul 2>nul && set "PY=python"
if not defined PY (
  echo Python 3 is needed once to extract the game data. Get it from python.org.
  exit /b 1
)
echo Extracting game data from your ROM...
pushd "%ROOT%nds\data"
%PY% "%ROOT%assets\restool.py" -r "%ROM%" --no-include-rom
if errorlevel 1 (
  popd
  exit /b 1
)
move /y smw_assets.dat smw_assets.bin >nul
popd
exit /b 0

:fail
echo.
echo BUILD FAILED - copy the messages above and send them to Claude.
pause
exit /b 1

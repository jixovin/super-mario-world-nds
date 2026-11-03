@echo off
rem Makes smw_assets.dat (the game data) from YOUR OWN Super Mario World (USA)
rem ROM, for the ready-made smw-nds.nds from the Releases page. No devkitPro
rem needed, only Python 3 (python.org).
rem
rem Use: drag your ROM (.sfc or .smc) onto this file, or put it in the roms
rem folder and double-click this file. Then copy smw_assets.dat to the
rem folder _nds\smw-nds\ on your DS's microSD card.
setlocal
set "ROOT=%~dp0"
set "ROM=%~1"
if not defined ROM set "ROM=%ROOT%roms\smw.sfc"
if not exist "%ROM%" for %%F in ("%ROOT%roms\*.sfc" "%ROOT%roms\*.smc") do if not defined FOUND (
  set "ROM=%%~fF"
  set "FOUND=1"
)
if not exist "%ROM%" (
  echo No ROM found. Drag your Super Mario World ^(USA^) ROM onto make_data.bat,
  echo or copy it into this folder and run make_data.bat again:
  echo   %ROOT%roms
  if not exist "%ROOT%roms" mkdir "%ROOT%roms"
  goto :fail
)
set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY where python >nul 2>nul && set "PY=python"
if not defined PY (
  echo Python 3 is needed. Get it from python.org, then run this again.
  goto :fail
)
echo Using ROM: %ROM%
pushd "%ROOT%"
%PY% "%ROOT%assets\restool.py" -r "%ROM%" --no-include-rom
if errorlevel 1 (
  popd
  goto :fail
)
popd
echo.
echo DONE: %ROOT%smw_assets.dat
echo Copy it to the folder _nds\smw-nds\ on your microSD card.
echo Keep it to yourself: it is made from your ROM.
pause
exit /b 0

:fail
echo.
echo FAILED - see "Troubleshooting" in README.md.
pause
exit /b 1

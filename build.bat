@echo off
rem Double-click to build the DS game with your installed devkitPro.
rem Output: nds\smw-nds.nds  (copy it to your R4 microSD card)
setlocal
set "MSYS_BASH=C:\devkitPro\msys2\usr\bin\bash.exe"
if not exist "%MSYS_BASH%" (
  echo devkitPro was not found at C:\devkitPro. Install the NDS Development package first.
  pause
  exit /b 1
)
"%MSYS_BASH%" -lc "cd \"$(cygpath -u '%~dp0nds')\" && make"
if errorlevel 1 (
  echo.
  echo BUILD FAILED - copy the messages above and send them to Claude.
) else (
  echo.
  echo BUILD OK: %~dp0nds\smw-nds.nds
)
pause

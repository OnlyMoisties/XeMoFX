@echo off
setlocal

set "PRX=%~1"
if "%PRX%"=="" set "PRX=%~dp0build\XeMoFXRaw.prx"

for %%I in ("%PRX%") do set "OUTDIR=%%~dpI"
for %%I in ("%PRX%") do set "RAWNAME=%%~nxI"
cd /d "%OUTDIR%"
echo [post-fx] Output dir: %OUTDIR%
echo [post-fx] Config    : %~2
echo [post-fx] Raw prx   : %RAWNAME%

if not exist "%RAWNAME%" goto :noraw

set "MKFSELF=%~dp0tools\make_fself.exe"
if not exist "%MKFSELF%" goto :nosigner

echo [post-fx] Signing %RAWNAME% to XeMoFX.prx ...
"%MKFSELF%" "%RAWNAME%" XeMoFX.prx
if errorlevel 1 goto :signfail
if not exist "XeMoFX.prx" goto :signfail
echo [post-fx] Signed OK - XeMoFX.prx is at %OUTDIR%XeMoFX.prx
goto :done

:noraw
echo [post-fx] WARNING: %RAWNAME% not found in %OUTDIR% - skipping.
goto :done
:nosigner
echo [post-fx] WARNING: tools\make_fself.exe not found - skipping sign.
goto :done
:signfail
echo [post-fx] WARNING: signing failed - raw prx is at %OUTDIR%%RAWNAME%.
goto :done

:done
endlocal
exit /b 0

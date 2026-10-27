@echo off
setlocal
cd /d "%~dp0"
set PSSLC="%SCE_ORBIS_SDK_DIR%\host_tools\bin\orbis-psslc.exe"
if not exist %PSSLC% set PSSLC="C:\Program Files (x86)\SCE\ORBIS SDKs\1.750\host_tools\bin\orbis-psslc.exe"
set GITBASH="C:\Program Files\Git\bin\bash.exe"

%PSSLC% -profile sce_vs_vs_orbis fx_vs.pssl -o fx_vs.sb || goto :err
%PSSLC% -profile sce_ps_orbis -DFX_BUFFER=0 fx_ps.pssl -o fx_ps_tex.sb || goto :err
%PSSLC% -profile sce_ps_orbis -DFX_BUFFER=1 fx_ps.pssl -o fx_ps_buf.sb || goto :err
%PSSLC% -profile sce_vs_vs_orbis ui_vs.pssl -o ui_vs.sb || goto :err
%PSSLC% -profile sce_ps_orbis ui_ps.pssl -o ui_ps.sb || goto :err

%GITBASH% -c "{ echo '#pragma once'; echo '#include <stdint.h>'; echo; for n in fx_vs fx_ps_tex fx_ps_buf ui_vs ui_ps; do printf 'static const uint8_t g_%%sSb[] __attribute__((aligned(16))) = {\n' $n; xxd -i < $n.sb; printf '};\n\n'; done; } > fx_shaders.h" || goto :err

echo Shaders rebuilt + embedded OK.
goto :eof
:err
echo Shader build FAILED.
exit /b 1

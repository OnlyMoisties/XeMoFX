@echo off
set VCTargetsPath=C:\Program Files (x86)\MSBuild\Microsoft.Cpp\v4.0\V110\
set SCE_ORBIS_SDK_DIR=C:\Program Files (x86)\SCE\ORBIS SDKs\1.750
"C:\Windows\Microsoft.NET\Framework\v4.0.30319\MSBuild.exe" "%~dp0XeMoFX.vcxproj" /p:Configuration=Prod /p:Platform=ORBIS /t:Build /v:minimal /nologo %*

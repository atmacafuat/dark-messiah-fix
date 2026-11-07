@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist dist mkdir dist
cl /nologo /O1 /W3 /utf-8 dmfix.c /Fe:dist\DMFix.exe /Fo:dist\ /link /LARGEADDRESSAWARE /SUBSYSTEM:WINDOWS user32.lib gdi32.lib advapi32.lib shell32.lib

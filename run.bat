@echo off
setlocal
cd /d "%~dp0out\build\release"
if not exist "PKGInstall.exe" (
    echo [ERRO] Executavel nao encontrado em out\build\release\PKGInstall.exe!
    echo Compile o projeto antes de executar.
    pause
    exit /b 1
)
start "" "PKGInstall.exe" %*
exit /b 0

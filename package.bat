@echo off
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%out\build\release"
set "STAGING=%ROOT%out\portable_staging"
set "ZIP_PATH=%ROOT%PKGInstall-win64-portable.zip"

echo ==========================================================
echo           Gerador de Pacote Portatil - PKGInstall
echo ==========================================================
echo.

:: 1. Localizar ambiente MSVC se necessario
where cl.exe >nul 2>&1
if errorlevel 1 (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
    )
)

:: 2. Localizar Qt e windeployqt
if not defined QTDIR (
    if exist "C:\Qt\6.9.3\msvc2022_64" (
        set "QTDIR=C:\Qt\6.9.3\msvc2022_64"
    ) else if exist "C:\Qt\6.9.2\msvc2022_64" (
        set "QTDIR=C:\Qt\6.9.2\msvc2022_64"
    )
)

set "WINDEPLOYQT=%QTDIR%\bin\windeployqt.exe"
if not exist "%WINDEPLOYQT%" (
    where windeployqt.exe >nul 2>&1
    if not errorlevel 1 (
        for /f "delims=" %%I in ('where windeployqt.exe') do set "WINDEPLOYQT=%%I"
    ) else (
        echo [ERRO] windeployqt.exe nao encontrado em "%WINDEPLOYQT%".
        echo Defina a variavel de ambiente QTDIR apontando para a instalacao do Qt MSVC 64-bit.
        pause
        exit /b 1
    )
)

:: 3. Compilar o projeto com Ninja
echo [1/4] Compilando PKGInstall (Release)...
where ninja.exe >nul 2>&1
if not errorlevel 1 (
    ninja -C "%BUILD_DIR%"
    if errorlevel 1 (
        echo [ERRO] Falha na compilacao com Ninja.
        pause
        exit /b 1
    )
) else (
    echo [AVISO] Ninja nao encontrado no PATH. Usando binario existente em "%BUILD_DIR%".
)

if not exist "%BUILD_DIR%\PKGInstall.exe" (
    echo [ERRO] Executavel "%BUILD_DIR%\PKGInstall.exe" nao encontrado!
    pause
    exit /b 1
)

:: 4. Preparar pasta de montagem
echo [2/4] Preparando pasta de montagem temporaria...
if exist "%STAGING%" rmdir /s /q "%STAGING%"
mkdir "%STAGING%"
mkdir "%STAGING%\qtplugins"

copy /y "%BUILD_DIR%\PKGInstall.exe" "%STAGING%\" >nul
copy /y "%ROOT%dist\qt.conf" "%STAGING%\" >nul

if exist "%BUILD_DIR%\D3Dcompiler_47.dll" (
    copy /y "%BUILD_DIR%\D3Dcompiler_47.dll" "%STAGING%\" >nul
)

:: 5. Implantar dependencias do Qt
echo [3/4] Implantando dependencias do Qt com windeployqt...
"%WINDEPLOYQT%" --plugindir "%STAGING%\qtplugins" --no-compiler-runtime --no-system-d3d-compiler --no-system-dxc-compiler --dir "%STAGING%" "%STAGING%\PKGInstall.exe" >nul
if errorlevel 1 (
    echo [AVISO] windeployqt retornou avisos durante a geracao.
)

:: 6. Compactar em ZIP
echo [4/4] Gerando arquivo ZIP portatil...
if exist "%ZIP_PATH%" del /f /q "%ZIP_PATH%"

powershell -NoProfile -Command "Compress-Archive -Path '%STAGING%\*' -DestinationPath '%ZIP_PATH%' -Force"
if errorlevel 1 (
    echo [ERRO] Falha ao criar arquivo ZIP.
    pause
    exit /b 1
)

:: 7. Limpeza
if exist "%STAGING%" rmdir /s /q "%STAGING%"

echo.
echo ==========================================================
echo  Sucesso! Pacote portatil gerado em:
echo  %ZIP_PATH%
echo ==========================================================
echo.
exit /b 0

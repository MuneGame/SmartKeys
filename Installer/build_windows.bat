@echo off
REM ============================================================================
REM  SmartKeys - compila los plugins, genera el instalador .exe (Windows x64)
REM  y lo ejecuta. El instalador BORRA ANTES cualquier version anterior de
REM  SmartKeys / SmartKeys FX y su ficha en la base de datos de FL Studio.
REM
REM  Requisitos: Visual Studio 2022 o posterior (C++), CMake 3.22+, Git, Inno Setup 6.3+
REM  Uso: doble clic o "Installer\build_windows.bat" desde la carpeta del proyecto
REM       (anade "noinstall" para generar el .exe sin instalarlo)
REM ============================================================================
setlocal
cd /d "%~dp0\.."
set "VERSION=1.3.0"
set "SETUP=Installer\Output\SmartKeys-%VERSION%-Windows-Setup.exe"

echo.
echo [1/4] Configurando CMake...
cmake -B build -A x64 -DSMARTKEYS_BUILD_FX=ON -DSMARTKEYS_COPY_AFTER_BUILD=OFF || goto :error

echo.
echo [2/4] Compilando Release (SmartKeys VST3 + Standalone, SmartKeys FX VST3)...
cmake --build build --config Release --parallel || goto :error

echo.
echo [3/4] Generando instalador con Inno Setup...
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo No se encontro ISCC.exe. Instala Inno Setup 6 desde https://jrsoftware.org/isdl.php
    goto :error
)
"%ISCC%" "Installer\Installer_Windows.iss" || goto :error

if /i "%~1"=="noinstall" goto :done

echo.
echo [4/4] Instalando (Windows pedira permiso de administrador)...
echo       El instalador borra primero la version anterior de SmartKeys.
"%SETUP%" /SILENT /SUPPRESSMSGBOXES /NORESTART /SP- || goto :error

:done
echo.
echo Listo: %SETUP%
echo En FL Studio: Options ^> Manage plugins ^> Find installed plugins.
pause
exit /b 0

:error
echo.
echo *** Error durante la compilacion o la instalacion ***
pause
exit /b 1

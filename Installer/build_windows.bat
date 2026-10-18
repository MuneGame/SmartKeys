@echo off
REM ============================================================================
REM  SmartKeys - compila el plugin y genera el instalador .exe (Windows x64)
REM  Requisitos: Visual Studio 2022 o posterior (C++), CMake 3.22+, Git, Inno Setup 6.3+
REM  Uso: doble clic o "Installer\build_windows.bat" desde la carpeta del proyecto
REM ============================================================================
setlocal
cd /d "%~dp0\.."

echo.
echo [1/3] Configurando CMake...
cmake -B build -A x64 || goto :error

echo.
echo [2/3] Compilando Release (VST3 + Standalone)...
cmake --build build --config Release --parallel || goto :error

echo.
echo [3/3] Generando instalador con Inno Setup...
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo No se encontro ISCC.exe. Instala Inno Setup 6 desde https://jrsoftware.org/isdl.php
    goto :error
)
"%ISCC%" "Installer\Installer_Windows.iss" || goto :error

echo.
echo Listo: Installer\Output\SmartKeys-1.0.0-Windows-Setup.exe
pause
exit /b 0

:error
echo.
echo *** Error durante la compilacion ***
pause
exit /b 1

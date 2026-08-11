@echo off
REM ============================================================
REM  Ghost Map Editor - build
REM  desenvolvido por Ghost - www.hkfirewall.com
REM
REM  Compila src\editor + src\shared e joga o exe direto em bin\,
REM  que e a pasta de execucao (o exe procura tools\aplicar.py e o
REM  editor.cfg ao lado dele).
REM
REM  Precisa do Visual Studio 2022 (ou 18) Community com o workload
REM  "Desenvolvimento para desktop com C++".
REM ============================================================
setlocal

REM 1o tenta o vswhere (acha qualquer edicao/ano instalado), depois os caminhos fixos
set VCVARS=
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
  echo [ERRO] vcvars64.bat nao encontrado. Ajuste o caminho no build.bat.
  exit /b 1
)
call "%VCVARS%" >nul

cd /d "%~dp0"
if not exist build mkdir build

set RAYLIB=third_party\raylib-6.0_win64_msvc16
set SHARED=src\shared
set SRC=src\editor\editor_main.cpp %SHARED%\viewer_core.cpp %SHARED%\pangya_gbin.cpp %SHARED%\pangya_pet.cpp %SHARED%\asset_db.cpp %SHARED%\dds_loader.cpp %SHARED%\platform_win.cpp %SHARED%\ground.cpp
set LIBS=%RAYLIB%\lib\raylib.lib opengl32.lib gdi32.lib winmm.lib shell32.lib user32.lib comdlg32.lib ole32.lib gdiplus.lib

echo [1/2] compilando recurso (icone + versao)...
rc /nologo /i src\editor /fo build\app.res src\editor\app.rc
if errorlevel 1 goto :erro

echo [2/2] compilando...
cl /nologo /O2 /EHsc /MD /std:c++17 /I"%RAYLIB%\include" /I"%SHARED%" ^
   /Fo:build\ /Fd:build\ %SRC% build\app.res ^
   /Fe:bin\GhostMapEditor.exe ^
   /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup %LIBS%
if errorlevel 1 goto :erro

echo.
echo [OK] bin\GhostMapEditor.exe gerado.
exit /b 0

:erro
echo.
echo [FALHOU] veja os erros acima.
exit /b 1

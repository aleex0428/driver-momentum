@echo off
rem Builds Driver Momentum with MSVC (Visual Studio 2022 or later, "Desktop development with C++").
rem   build.bat                -> native\driver_momentum.dll (same as "build.bat dll")
rem   build.bat test           -> the DLL, then model_test (the form and streak engine over whole careers) and
rem                               career_test (the book, the races and the sprints against a fake core)
rem   build.bat model          -> only model_test; "career" likewise
rem   build.bat preview <dir>  -> draws the Form Guide panel into SVG files (tools\render_svgs.py makes PNGs)
rem native\build.sh still builds the DLL with zig, as the original Driver Form did.
rem The DLL is linked with its PDB (build\<name>.pdb, never packaged): /PDBALTPATH:%%_PDB%% keeps this PC's path out of
rem the DLL. tools\crash_lab.py stores each version's PDB and names our frames in crash reports.
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo No se encuentra vswhere.exe: instala Visual Studio con "Desarrollo para el escritorio con C++".
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio no tiene las herramientas de C++ x64 instaladas.
    exit /b 1
)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist build mkdir build
set FLAGS=/nologo /std:c++latest /EHsc /O2 /MT /W3 /Zc:preprocessor /Zc:__cplusplus /utf-8 /permissive- /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /Z7 /I include /Fobuild\
rem the addon's version, from the manifest, for the line it logs when it loads
set "DMVER=dev"
for /f "tokens=2 delims=:, " %%v in ('findstr /c:"\"version\"" ..\swofty.json') do if "!DMVER!"=="dev" set "DMVER=%%~v"
set FLAGS=%FLAGS% /DDM_VERSION=\"%DMVER%\"

set "WHAT=%~1"
if "%WHAT%"=="" set "WHAT=dll"
if /i "%WHAT%"=="preview" goto preview
set DLL=0
set TESTS=
if /i "%WHAT%"=="dll" set DLL=1
if /i "%WHAT%"=="test" (
    set DLL=1
    set "TESTS=model career"
)
if /i "%WHAT%"=="model" set "TESTS=model"
if /i "%WHAT%"=="career" set "TESTS=career"
if "%DLL%"=="0" if "%TESTS%"=="" (
    echo build.bat: unknown target "%WHAT%" ^(dll, test, model, career, preview^)
    exit /b 1
)

set "SRC=src\momentum.cpp"
if "%DLL%"=="1" set "SRC=!SRC! src\driver_form.cpp"
for %%t in (%TESTS%) do set "SRC=!SRC! tests\%%t_test.cpp"
cl %FLAGS% /MP /c %SRC% || exit /b 1
if "%DLL%"=="1" (
    link /NOLOGO /DLL build\driver_form.obj build\momentum.obj /OUT:driver_momentum.dll /IMPLIB:build\driver_momentum.lib /DEBUG /PDB:build\driver_momentum.pdb /PDBALTPATH:%%_PDB%% /OPT:REF /OPT:ICF /INCREMENTAL:NO || exit /b 1
    del /q driver_momentum.exp 2>nul
    echo built %CD%\driver_momentum.dll
)
for %%t in (%TESTS%) do (
    link /NOLOGO build\%%t_test.obj build\momentum.obj /OUT:build\%%t_test.exe || exit /b 1
    build\%%t_test.exe || exit /b 1
)
endlocal
exit /b 0

:preview
if "%~2"=="" (
    echo build.bat preview ^<dir^>
    exit /b 1
)
cl %FLAGS% /LD tests\core_stub.cpp /Fe:build\swofty_core.dll /link /NOLOGO /NOIMPLIB >nul || exit /b 1
cl %FLAGS% /MP /c src\momentum.cpp tests\form_guide_preview.cpp || exit /b 1
link /NOLOGO build\form_guide_preview.obj build\momentum.obj /OUT:build\form_guide_preview.exe || exit /b 1
build\form_guide_preview.exe %2 || exit /b 1
endlocal
exit /b 0

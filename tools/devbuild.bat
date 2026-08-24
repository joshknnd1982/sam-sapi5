@echo off
REM Quick standalone build of the engine + sam_render, for development and for
REM the Python-vs-C++ verification. The real build is CMake (see build_all.bat).
setlocal
set ARCH=%1
if "%ARCH%"=="" set ARCH=x64

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found; install Visual Studio 2022 Build Tools.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo ERROR: no Visual Studio installation found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
if errorlevel 1 exit /b 1

set ROOT=%~dp0..
set OUT=%ROOT%\build_dev\%ARCH%
if not exist "%OUT%" mkdir "%OUT%"

cl /nologo /std:c++17 /O2 /EHsc /W3 /MT /D_CRT_SECURE_NO_WARNINGS ^
   /Fo"%OUT%\\" /Fe"%OUT%\sam_render.exe" ^
   "%ROOT%\tools\sam_render.cpp" ^
   "%ROOT%\src\engine\sam_tables.cpp" ^
   "%ROOT%\src\engine\sam_reciter.cpp" ^
   "%ROOT%\src\engine\sam_cmudict.cpp" ^
   "%ROOT%\src\engine\sam_parser.cpp" ^
   "%ROOT%\src\engine\sam_renderer.cpp" ^
   "%ROOT%\src\engine\sam_engine.cpp"
if errorlevel 1 exit /b 1

echo Built %OUT%\sam_render.exe
endlocal

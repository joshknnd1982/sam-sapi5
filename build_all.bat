@echo off
REM Full build: 64-bit engine + utility, 32-bit engine, then the installer.
setlocal enabledelayedexpansion

echo ============================================
echo  SAM Voice for SAPI5 - full build
echo ============================================
echo.

set ROOT=%~dp0
set OUTPUT=%ROOT%output

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Install Visual Studio 2022 Build Tools.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo ERROR: no Visual Studio installation found.
    exit /b 1
)
echo Visual Studio: %VSDIR%

set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo ERROR: Inno Setup 6 not found.
    exit /b 1
)
echo Inno Setup:    %ISCC%
echo.

if not exist "%OUTPUT%" mkdir "%OUTPUT%"

echo --- regenerating engine tables from the Python reference ---
where python >nul 2>&1
if errorlevel 1 (
    echo   python not on PATH; keeping the generated tables as they are.
) else (
    pushd "%ROOT%bin"
    python "%ROOT%tools\gen_tables.py"
    if errorlevel 1 (
        echo ERROR: table generation failed.
        popd
        exit /b 1
    )
    popd
)
echo.

echo --- building x64 ---
cmake -A x64 -S "%ROOT%." -B "%ROOT%build_x64"
if errorlevel 1 exit /b 1
cmake --build "%ROOT%build_x64" --config Release
if errorlevel 1 exit /b 1
echo.

echo --- building x86 ---
cmake -A Win32 -S "%ROOT%." -B "%ROOT%build_x86"
if errorlevel 1 exit /b 1
cmake --build "%ROOT%build_x86" --config Release
if errorlevel 1 exit /b 1
echo.

echo --- verifying the C++ engine against the Python reference ---
where python >nul 2>&1
if errorlevel 1 (
    echo   python not on PATH; skipping verification.
) else (
    copy /Y "%ROOT%bin\cmudict.txt" "%ROOT%build_x64\bin\Release\" >nul
    python "%ROOT%tools\verify_engine.py"
    if errorlevel 1 (
        echo ERROR: the C++ engine does not match the Python reference.
        exit /b 1
    )
)
echo.

echo --- building the installer ---
"%ISCC%" /O"%OUTPUT%" "%ROOT%installer\sam_sapi5.iss"
if errorlevel 1 exit /b 1
echo.

echo ============================================
echo  Build complete
echo ============================================
echo   Installer:  %OUTPUT%\SamVoiceSAPI5_Setup.exe
echo   x64 engine: %ROOT%build_x64\bin\Release\SamVoiceSAPI.dll
echo   x86 engine: %ROOT%build_x86\bin\Release\SamVoiceSAPI.dll
echo   Utility:    %ROOT%build_x64\bin\Release\SamVoiceSettings.exe
echo.
endlocal

@echo off
REM Quick standalone build of the SAPI5 DLL, for compile-error checking.
setlocal
set ARCH=%1
if "%ARCH%"=="" set ARCH=x64

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
if errorlevel 1 exit /b 1

set ROOT=%~dp0..
set OUT=%ROOT%\build_dev\%ARCH%
set OBJ=%OUT%\obj_dll
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

cl /nologo /std:c++17 /O2 /EHsc /W3 /MT /LD ^
   /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DUNICODE /D_UNICODE ^
   /I"%ROOT%\src\engine" /I"%ROOT%\src\sapi" ^
   /Fo"%OBJ%\\" /Fe"%OUT%\SamVoiceSAPI.dll" ^
   "%ROOT%\src\sapi\sapi_main.cpp" ^
   "%ROOT%\src\sapi\com.cpp" ^
   "%ROOT%\src\sapi\registry.cpp" ^
   "%ROOT%\src\sapi\ISpDataKeyImpl.cpp" ^
   "%ROOT%\src\sapi\IEnumSpObjectTokensImpl.cpp" ^
   "%ROOT%\src\sapi\ISpTTSEngineImpl.cpp" ^
   "%ROOT%\src\sapi\voice_token.cpp" ^
   "%ROOT%\src\sapi\sam_paths.cpp" ^
   "%ROOT%\src\sapi\sam_log.cpp" ^
   "%ROOT%\src\sapi\sam_settings.cpp" ^
   "%ROOT%\src\engine\sam_tables.cpp" ^
   "%ROOT%\src\engine\sam_reciter.cpp" ^
   "%ROOT%\src\engine\sam_cmudict.cpp" ^
   "%ROOT%\src\engine\sam_parser.cpp" ^
   "%ROOT%\src\engine\sam_renderer.cpp" ^
   "%ROOT%\src\engine\sam_engine.cpp" ^
   /link /DEF:"%ROOT%\src\sapi\sam_sapi.def" ^
   ole32.lib oleaut32.lib advapi32.lib shell32.lib
if errorlevel 1 exit /b 1

echo Built %OUT%\SamVoiceSAPI.dll
endlocal

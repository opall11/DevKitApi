@echo off
rem Build DevKitApi v0 — MSVC 14.44 (toolset v143 de VS2022, embarque dans VS 2026), x64, /MD, C++17, PDB.
rem Output : ..\build\UE4Editor-DevKitApi.dll (+ .pdb), next to this src folder
setlocal
set SRC=%~dp0
set OUT=%~dp0..\build
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44 >nul
if errorlevel 1 ( echo vcvarsall KO & exit /b 1 )
echo [cl] %_VCVARS_VER% / toolset:
cl 2>&1 | findstr /i "Version"
pushd "%OUT%"
rc /nologo /fo DevKitApi.res "%SRC%DevKitApi.rc" || ( popd & exit /b 1 )
rem /MT (CRT statique) et non /MD : le DevKit embarque un vieux MSVCP140.dll 14.16 (VS2017) dans
rem Projects\ShooterGame\Binaries\Win64, pris en priorite par le chargeur -> crash 0xC0000005 dans MSVCP140
rem avec un binaire 14.44 (constate le 2026-09-03). En /MT la DLL n'importe plus aucun runtime VC.
cl /nologo /W4 /O2 /MT /EHsc /std:c++17 /Zi /DUNICODE /D_UNICODE /DWIN32 /D_WINDOWS /D_WIN32_WINNT=0x0601 ^
   /Fo"%OUT%\\" /Fd"%OUT%\DevKitApi_obj.pdb" /c "%SRC%DevKitApi.cpp" || ( popd & exit /b 1 )
link /nologo /DLL /MACHINE:X64 /DEBUG:FULL /INCREMENTAL:NO /OPT:REF /OPT:ICF ^
   /OUT:"%OUT%\UE4Editor-DevKitApi.dll" /PDB:"%OUT%\UE4Editor-DevKitApi.pdb" /IMPLIB:"%OUT%\UE4Editor-DevKitApi.lib" ^
   "%OUT%\DevKitApi.obj" "%OUT%\DevKitApi.res" ws2_32.lib user32.lib kernel32.lib || ( popd & exit /b 1 )
popd
echo.
echo BUILD OK : %OUT%\UE4Editor-DevKitApi.dll
dir "%OUT%\UE4Editor-DevKitApi.dll" "%OUT%\UE4Editor-DevKitApi.pdb" | findstr /i "DevKitApi"
endlocal

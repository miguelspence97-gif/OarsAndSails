@echo off
rem RowRim quick build: x64 Native Tools (VS 2022) + cmake release preset
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo vswhere.exe not found. Is Visual Studio 2022 installed?
  goto :fail
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version [17.0^,18.0^) -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo Visual Studio 2022 with the C++ x64 tools was not found.
  goto :fail
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :fail

if not exist "build\msvc\build.ninja" (
  echo Configuring RowRim ^(first build, vcpkg may take a while^)...
  cmake --preset release-windows
  if errorlevel 1 goto :fail
)

echo Building RowRim (release-windows)...
cmake --build --preset release-windows
if errorlevel 1 goto :fail

echo.
echo Deploying to the MO2 test build...
set "OUT=build\msvc"
set "DEST=E:\Modding2\MO2\mods\RowRim (Test Build)\SKSE\Plugins"
if not exist "%OUT%\RowRim.dll" (
  echo Built DLL not found in %OUT%
  goto :fail
)
if not exist "%DEST%" mkdir "%DEST%"
copy /Y "%OUT%\RowRim.dll" "%DEST%\" >nul
if errorlevel 1 (
  echo Could not copy the DLL. Is Skyrim still running?
  goto :fail
)
if exist "%OUT%\RowRim.pdb" copy /Y "%OUT%\RowRim.pdb" "%DEST%\" >nul
echo Deployed to %DEST%
rem v0.7.1: everything under Data (meshes, rowing animation + Nemesis list, textures, RowRim.esl, docks ini)
set "MODROOT=E:\Modding2\MO2\mods\RowRim (Test Build)"
rem v1.7.8: /D = only files newer than the deployed ones (a RowRim_Docks.ini changed in game by oas "dock add" is kept)
if exist "Data" xcopy /E /I /Y /Q /D "Data\*" "%MODROOT%\" >nul
rem v1.7.9: the Board / Row / Steer prompts come from OarsAndSails.esl (tick it; untick the old Oars and Sails mod)
if exist "release\OarsAndSails.esl" copy /Y "release\OarsAndSails.esl" "%MODROOT%\" >nul
echo Data copied to %MODROOT% (enable RowRim.esl once; run Nemesis after the animation files change)
rem v1.7.12: NEFARAM gets the release layout, as a player installs it over Oars and Sails: mods\OarsAndSails_v with
rem SKSE\Plugins\OarsAndSails.dll (= RowRim.dll), OarsAndSails.esl, the row clip in the OarsAndSails animation folder
set "NEF=E:\Modlists\NEFARAM\mods\OarsAndSails_v"
if not exist "E:\Modlists\NEFARAM\mods" goto :nefdone
if not exist "%NEF%\SKSE\Plugins" mkdir "%NEF%\SKSE\Plugins"
copy /Y "%OUT%\RowRim.dll" "%NEF%\SKSE\Plugins\OarsAndSails.dll" >nul
if errorlevel 1 goto :neffail
if exist "%OUT%\RowRim.pdb" copy /Y "%OUT%\RowRim.pdb" "%NEF%\SKSE\Plugins\OarsAndSails.pdb" >nul
robocopy "Data" "%NEF%" /E /XO /NFL /NDL /NJH /NJS /NP /XD RowRimRowing /XF FNIS_RowRimRowing_Behavior.hkx RowRim.esl >nul
if not exist "%NEF%\meshes\actors\character\animations\OarsAndSails" mkdir "%NEF%\meshes\actors\character\animations\OarsAndSails"
copy /Y "Data\meshes\actors\character\animations\RowRimRowing\RowboatRow_Loopv3.hkx" "%NEF%\meshes\actors\character\animations\OarsAndSails\" >nul
if exist "release\OarsAndSails.esl" copy /Y "release\OarsAndSails.esl" "%NEF%\" >nul
echo Also deployed to %NEF% (release layout: OarsAndSails.dll)
goto :nefdone
:neffail
echo Could not copy the DLL to NEFARAM. Is Skyrim still running?
goto :fail
:nefdone
rem v1.7.47: the Default MO2 profile plays the "Oars and Sails" mod (RowRim (Test Build) is not in it): the same
rem release layout goes there too, so a build always reaches the game
set "OAS=E:\Modding2\MO2\mods\Oars and Sails"
if not exist "%OAS%" goto :oasdone
if not exist "%OAS%\SKSE\Plugins" mkdir "%OAS%\SKSE\Plugins"
copy /Y "%OUT%\RowRim.dll" "%OAS%\SKSE\Plugins\OarsAndSails.dll" >nul
if errorlevel 1 (
  echo Could not copy the DLL to the Oars and Sails mod. Is Skyrim still running?
  goto :fail
)
robocopy "Data" "%OAS%" /E /XO /NFL /NDL /NJH /NJS /NP /XD RowRimRowing /XF FNIS_RowRimRowing_Behavior.hkx RowRim.esl >nul
if exist "release\OarsAndSails.esl" copy /Y "release\OarsAndSails.esl" "%OAS%\" >nul
echo Also deployed to %OAS% (the Default profile's mod)
:oasdone

echo.
echo BUILD OK
pause
exit /b 0

:fail
echo.
echo BUILD FAILED
pause
exit /b 1

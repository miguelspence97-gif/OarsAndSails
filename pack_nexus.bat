@echo off
rem v1.7.9: the Nexus zip, made to install OVER Oars and Sails (MO2 merge / replace, Vortex update): same file names where
rem OAS had them, so nothing old is left running:
rem   SKSE\Plugins\OarsAndSails.dll       = RowRim.dll (overwrites the old plugin)
rem   OarsAndSails.esl                    = the prompt / old-save stub (overwrites the old one, already ticked)
rem   animations\OarsAndSails\...          = RowRim's row clip under the OAS name and list (same event, same path:
rem                                          no Nemesis / Pandora re-run needed), the OAS GPMA files kept as they were
rem v1.7.40: textures the release does not use (the tent is off) and source images are left out
rem v1.7.55: also the v1.7.48 sleeping bag's textures (the bed is the camp bed now)
rem release\oas_overlay.zip holds those OAS-named files. Build first (build.bat).
setlocal
cd /d "%~dp0"
set "VER=v1.7.57"
rem v1.7.20: the zip is named just "Oars and Sails" so MO2 offers it as the existing Oars and Sails mod (Replace);
rem the version goes in Nexus's version field, not the file name
set "ZIP=dist\Oars and Sails.zip"
set "STAGE=dist\stage"
if exist "%STAGE%" rmdir /S /Q "%STAGE%"
mkdir "%STAGE%\Data\SKSE\Plugins"
if not exist "build\msvc\RowRim.dll" (
  echo build\msvc\RowRim.dll not found: run build.bat first
  goto :fail
)
robocopy "Data" "%STAGE%\Data" /E /NFL /NDL /NJH /NJS /NP /XD RowRimRowing /XF FNIS_RowRimRowing_Behavior.hkx RowRim.esl boat_tent_d.dds boat_tent_n.dds boat_tentpole_d.dds boat_canvas_d.dds boat_canvas_n.dds boat_bench_d.dds fishbone_d.dds boat_sleepingbag_d.dds boat_sleepingbag_n.dds *.jpg *.png *.pdb >nul
if errorlevel 8 goto :fail
powershell -NoProfile -Command "Expand-Archive -Force -Path 'release\oas_overlay.zip' -DestinationPath '%STAGE%'"
if errorlevel 1 goto :fail
copy /Y "build\msvc\RowRim.dll" "%STAGE%\Data\SKSE\Plugins\OarsAndSails.dll" >nul
copy /Y "release\README.txt" "%STAGE%\" >nul
copy /Y "release\TUNE_SHEET.txt" "%STAGE%\" >nul
copy /Y "LICENSE.txt" "%STAGE%\" >nul
if exist "%ZIP%" del /Q "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path '%STAGE%\*' -DestinationPath '%ZIP%'"
if errorlevel 1 goto :fail
echo.
echo %ZIP% written (%VER%, install it over Oars and Sails)
pause
exit /b 0
:fail
echo PACK FAILED
pause
exit /b 1

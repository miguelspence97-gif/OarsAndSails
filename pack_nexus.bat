@echo off
rem v1.7.9: the Nexus zip, made to install OVER Oars and Sails (MO2 merge / replace, Vortex update): same file names where
rem OAS had them, so nothing old is left running:
rem   SKSE\Plugins\OarsAndSails.dll       = RowRim.dll (overwrites the old plugin)
rem   OarsAndSails.esl                    = the prompt / old-save stub (overwrites the old one, already ticked)
rem   animations\OarsAndSails\...          = RowRim's row clip under the OAS name and list (same event, same path:
rem                                          no Nemesis / Pandora re-run needed), the OAS GPMA files kept as they were
rem v1.7.40: textures the release does not use and source images are left out (v1.8.24: the tent is back, its textures ship)
rem v1.7.55: also the v1.7.48 sleeping bag's textures (the bed is the camp bed now)
rem v1.7.73: boat_canvas_n.dds ships again (the sail's normal map)
rem v1.7.66: the bundled Offset Movement Animation (Nemesis_Engine\mod\gpma, GPMA*.hkx) is out of the overlay (it overwrote
rem the real mod and hid it from Pandora); readme and license named "OarsAndSails ..." (no MO2 conflicts)
rem release\oas_overlay.zip holds those OAS-named files. Build first (build.bat).
setlocal
cd /d "%~dp0"
set "VER=v1.8.53"
rem v1.8.8: the longship stays out too (RowRim_Longship*.nif, textures\RowRim\Longship)
rem v1.8.0 (Nexus 1.0.86): the sailing boat stays out (local test: PolyCrate model, RowRim_SailBoat*.nif, textures\RowRim\SailBoat)
rem v1.8.53 (Nexus 1.0.87): the sailing boat ships (PolyCrate permission: converted .nif/.dds only, credited in the readme):
rem   RowRim_SailBoatSkin.nif, RowRim_SailBoatHull.nif, textures\RowRim\SailBoat. The old plain-sails model and the cloth
rem   test (RowRim_SailBoat.nif, RowRim_SailBoatCloth.nif) and the whole longship stay out. The overlay now carries the
rem   v1.8.40 helm idles (HelmTiller_*.hkx, FNIS_OarsAndSails_Behavior.hkx with them built in) and the 783-byte esl.
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
robocopy "Data" "%STAGE%\Data" /E /NFL /NDL /NJH /NJS /NP /XD RowRimRowing Longship /XF RowRim_SailBoat.nif RowRim_SailBoatCloth.nif RowRim_Longship*.nif FNIS_RowRimRowing_Behavior.hkx RowRim.esl boat_canvas_d.dds boat_bench_d.dds fishbone_d.dds boat_sleepingbag_d.dds boat_sleepingbag_n.dds RowRim_Swoosh.nif swoosh_streak.dds *.jpg *.png *.pdb >nul
if errorlevel 8 goto :fail
powershell -NoProfile -Command "Expand-Archive -Force -Path 'release\oas_overlay.zip' -DestinationPath '%STAGE%'"
if errorlevel 1 goto :fail
copy /Y "build\msvc\RowRim.dll" "%STAGE%\Data\SKSE\Plugins\OarsAndSails.dll" >nul
copy /Y "release\README.txt" "%STAGE%\OarsAndSails README.txt" >nul
copy /Y "release\TUNE_SHEET.txt" "%STAGE%\" >nul
copy /Y "LICENSE.txt" "%STAGE%\OarsAndSails LICENSE.txt" >nul
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

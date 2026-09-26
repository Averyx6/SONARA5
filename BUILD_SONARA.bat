@echo off
setlocal EnableExtensions
cd /d "%~dp0"
if not exist DIST mkdir DIST
set LOG=DIST\BUILD_LOG.txt
(
  echo SONARA Windows x64 Release Build
  echo Started %DATE% %TIME%
  echo.
  cmake -S . -B build-win64 -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON || exit /b 1
  cmake --build build-win64 --config Release --parallel || exit /b 1
  ctest --test-dir build-win64 -C Release --output-on-failure || exit /b 1
  powershell -NoProfile -ExecutionPolicy Bypass -Command "$vst=Get-ChildItem build-win64 -Recurse -Directory -Filter 'SONARA.vst3'|Select-Object -First 1;if(-not $vst){throw 'SONARA.vst3 not found'};Remove-Item DIST\SONARA.vst3 -Recurse -Force -ErrorAction SilentlyContinue;Copy-Item $vst.FullName DIST\SONARA.vst3 -Recurse -Force;Compress-Archive -Path DIST\SONARA.vst3 -DestinationPath DIST\SONARA-Windows-x64-VST3.zip -Force" || exit /b 1
  echo.
  echo Finished %DATE% %TIME%
) > "%LOG%" 2>&1
if errorlevel 1 (
  type "%LOG%"
  echo BUILD FAILED
  exit /b 1
)
type "%LOG%"
echo BUILD OK: DIST\SONARA-Windows-x64-VST3.zip
endlocal
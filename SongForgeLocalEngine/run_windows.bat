@echo off
setlocal
set "ACE=%LOCALAPPDATA%\SongForgeLocal\ACE-Step-1.5"
if not exist "%ACE%" (
  echo SongForge Local is not installed yet.
  echo Run install_and_run_windows.ps1 first.
  pause
  exit /b 1
)
cd /d "%ACE%"
set ACESTEP_API_HOST=0.0.0.0
set ACESTEP_API_PORT=8001
set ACESTEP_API_KEY=
set ACESTEP_INIT_LLM=auto

where uv >nul 2>nul
if %errorlevel%==0 (
  uv run acestep-api
) else (
  "%USERPROFILE%\.local\bin\uv.exe" run acestep-api
)
pause

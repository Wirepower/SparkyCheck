@echo off
setlocal
cd /d "%~dp0\.."
python tools\embed_boot_logo.py
if errorlevel 1 (
  echo.
  echo Failed to regenerate include\boot_logo_embedded.h
  exit /b 1
)
echo.
echo Boot logo embedded header regenerated successfully.
endlocal


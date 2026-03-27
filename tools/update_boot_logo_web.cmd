@echo off
setlocal
cd /d "%~dp0\.."
python tools\embed_boot_logo.py assets\boot_logoweb.bmp
if errorlevel 1 (
  echo.
  echo Failed to regenerate web logo header.
  exit /b 1
)
copy /Y include\boot_logo_embedded.h include\boot_logo_web_embedded.h >nul
python tools\embed_boot_logo.py assets\boot_logo.bmp >nul
echo.
echo Web logo header regenerated successfully from assets\boot_logoweb.bmp
endlocal

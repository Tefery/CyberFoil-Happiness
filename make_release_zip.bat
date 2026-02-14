@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "NRO_FILE=%SCRIPT_DIR%cyberfoil.nro"
set "OUTPUT_ZIP=%SCRIPT_DIR%cyberfoil.zip"
set "STAGE_DIR=%SCRIPT_DIR%release_zip_stage"
set "TARGET_DIR=%STAGE_DIR%\switch\CyberFoil"

if not exist "%NRO_FILE%" (
  echo ERROR: Missing NRO file: "%NRO_FILE%"
  echo Build first, then run this script again.
  exit /b 1
)

if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"
mkdir "%TARGET_DIR%" || (
  echo ERROR: Failed to create staging directory.
  exit /b 1
)

copy /y "%NRO_FILE%" "%TARGET_DIR%\cyberfoil.nro" >nul || (
  echo ERROR: Failed to copy cyberfoil.nro into staging directory.
  exit /b 1
)

if exist "%OUTPUT_ZIP%" del /f /q "%OUTPUT_ZIP%"

powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%STAGE_DIR%\switch' -DestinationPath '%OUTPUT_ZIP%' -Force"
if errorlevel 1 (
  echo ERROR: Failed to create "%OUTPUT_ZIP%".
  if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"
  exit /b 1
)

if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"

echo Release package created: "%OUTPUT_ZIP%"
echo Zip structure: switch\CyberFoil\cyberfoil.nro
exit /b 0

@echo off
setlocal
set "VCVARS=E:\VisualStudio\VS2026\VC\Auxiliary\Build\vcvars64.bat"
set "MOSQUITTO_DIR=C:\Program Files\mosquitto"
for %%I in ("%~dp0..\..") do set "PROJECT_ROOT=%%~fI"
if not defined OUTPUT_EXE set "OUTPUT_EXE=%PROJECT_ROOT%\bin\windows\gateway.exe"

if not exist "%VCVARS%" (
    echo Khong tim thay MSVC: %VCVARS%
    exit /b 1
)
if not exist "%MOSQUITTO_DIR%\devel\mosquitto.lib" (
    echo Khong tim thay mosquitto.lib
    exit /b 1
)

call "%VCVARS%" >nul
if not exist "%PROJECT_ROOT%\bin\windows" mkdir "%PROJECT_ROOT%\bin\windows"

pushd "%PROJECT_ROOT%"
cl /nologo /W4 /O2 /std:c11 src\main.c src\server_client.c src\gateway_config.c /Isrc /I"%MOSQUITTO_DIR%\devel" /Fe:"%OUTPUT_EXE%" /link /LIBPATH:"%MOSQUITTO_DIR%\devel" mosquitto.lib ws2_32.lib
if errorlevel 1 exit /b %errorlevel%

del /q main.obj server_client.obj gateway_config.obj 2>nul
popd

echo Build thanh cong: %OUTPUT_EXE%

@echo off
echo Dang khoi dong MQTT broker va Zigbee2MQTT...

start "MQTT Mosquitto" powershell -NoExit -Command "cd 'C:\Program Files\mosquitto'; .\mosquitto.exe -p 1885 -v"

timeout /t 5 /nobreak >nul

start "Zigbee2MQTT" powershell -NoExit -Command "cd 'C:\zigbee2mqtt'; npm start"

timeout /t 10 /nobreak >nul

exit /b 0
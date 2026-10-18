@echo off
REM Endurance: run all patterns for 5 minutes continuously
set /p HOST="Server IP [127.0.0.1]: " || set HOST=127.0.0.1
set /p PORT="Server Port [25565]: " || set PORT=25565
if "%HOST%"=="" set HOST=127.0.0.1
if "%PORT%"=="" set PORT=19132
python "%~dp0stress_test.py" %HOST% %PORT% --cycles 0 --duration 300 --bots 512 --burst 32 --move --hold 10 30 --ramp 0.1 --quiet
pause

@echo off
REM Aggressive: short holds, fast ramp - hammers the players.erase() path
set /p HOST="Server IP [127.0.0.1]: " || set HOST=127.0.0.1
set /p PORT="Server Port [19132]: " || set PORT=19132
if "%HOST%"=="" set HOST=127.0.0.1
if "%PORT%"=="" set PORT=19132
python "%~dp0stress_test.py" %HOST% %PORT% --bots 12 --hold 0.5 2 --ramp 0.2
pause

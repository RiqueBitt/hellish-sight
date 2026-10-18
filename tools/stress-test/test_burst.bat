@echo off
REM Burst joins - 4 bots connect simultaneously
set /p HOST="Server IP [127.0.0.1]: " || set HOST=127.0.0.1
set /p PORT="Server Port [19132]: " || set PORT=19132
if "%HOST%"=="" set HOST=127.0.0.1
if "%PORT%"=="" set PORT=19132
python "%~dp0stress_test.py" %HOST% %PORT% --burst 4 --hold 1 3
pause

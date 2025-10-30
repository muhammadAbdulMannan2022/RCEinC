@echo off
rem compile with winsock lib
gcc main.c -o main.exe -lws2_32
if errorlevel 1 (
  echo Compile failed.
  pause
  goto :eof
)

echo Compile OK — launching server in a new window...
start "" "%~dp0main.exe"
echo Server started (new window). Press any key to close this launcher.
pause

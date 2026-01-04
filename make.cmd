@echo off
setlocal

if "%1"=="clean" goto clean

set SOURCES=%*
set MAIN=%1

for %%F in (%MAIN%) do (
    set MAIN_NAME=%%~nF
    set ext=%%~xF
)
if "%ext%"=="" set ext=.cpp

if not exist build mkdir build

set CMD_OPTS=/EHsc /std:c++20 /W2 /WX /permissive- /Zi /O2 /MT 
set EXECUTABLE_NAME=%MAIN_NAME%.exe

echo cl.exe %CMD_OPTS% /Fo:build\ /Fd:build\%MAIN_NAME%.pdb /Fe:build\%EXECUTABLE_NAME% %SOURCES%
cl.exe %CMD_OPTS% /Fo:build\ /Fd:build\%MAIN_NAME%.pdb /Fe:build\%EXECUTABLE_NAME% %SOURCES%

goto :EOF

:clean
if exist build rd /s /q build
echo Cleaned build directory.
goto :EOF

endlocal

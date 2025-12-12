@echo off
setlocal enabledelayedexpansion

if "%1"=="clean" goto clean

set PARAM=%1
if "%PARAM%"=="" set PARAM=op_copy.cpp

for %%F in (%PARAM%) do (
    set fname=%%~nF
    set ext=%%~xF
)
if "%ext%"=="" set ext=.cpp

if not exist build mkdir build

set CMD_OPTS=/EHsc /std:c++20 /W4 /Zi /O2 /MT 
set FILES=%fname%%ext%

if /I "%fname%"=="offset2lba" (
    set FILES=offset2lba.cpp offset2lba_windows.cpp
)

cl.exe %CMD_OPTS% /Fo:build\ /Fd:build\%fname%.pdb /Fe:build\%fname%.exe %FILES%

goto :EOF

:clean
if exist build rd /s /q build
echo Cleaned build directory.
goto :EOF

endlocal

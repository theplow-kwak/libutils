@echo off
setlocal enabledelayedexpansion

set PARAM=%1
if "%PARAM%"=="" set PARAM=op_copy.cpp

for %%F in (%PARAM%) do (
    set fname=%%~nF
    set ext=%%~xF
)

if "%ext%"=="" set ext=.cpp

if /I "%fname%"=="offset2lba" (
    cl.exe /EHsc /W4 /O2 /Fe:offset2lba.exe offset2lba.cpp offset2lba_windows.cpp
   goto :EOF
)
cl /std:c++20 /W4 /Zi /Od /MT /Fe:%fname%.exe %fname%%ext%

endlocal

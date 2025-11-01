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
    set libs=offset2lba_windows.cpp
    set cflags=/DUNICODE /D_UNICODE
)
if /I "%fname%"=="fs_trans" (
    set libs=file_translation.cpp
    set cflags=/DUNICODE /D_UNICODE
)
if /I "%fname%"=="pcispace" (
    set libs=setupapi.lib cfgmgr32.lib wbemuuid.lib
)

echo cl /std:c++20 /EHsc /W4 /Zi /Od /MT %cflags% /Fe:%fname%.exe %fname%%ext% %libs%
cl /std:c++20 /EHsc /W4 /Zi /Od /MT %cflags% /Fe:%fname%.exe %fname%%ext% %libs%

endlocal

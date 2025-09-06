@echo off
setlocal enabledelayedexpansion

:: 기본값은 op_copy.cpp
set PARAM=%1
if "%PARAM%"=="" set PARAM=op_copy.cpp

:: 파일명과 확장자 분리
for %%F in (%PARAM%) do (
    set fname=%%~nF
    set ext=%%~xF
)

if "%ext%"=="" set ext=.cpp

:: cl.exe 실행
cl /std:c++20 /W4 /Zi /Od /MT /Fe:%fname%.exe %fname%%ext%

endlocal

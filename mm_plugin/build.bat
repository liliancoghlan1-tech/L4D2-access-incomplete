@echo off
setlocal

set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
set SRC=..\mm_plugin\src
set GAME=C:\Program Files (x86)\Steam\steamapps\common\Left 4 Dead 2\left4dead2\addons

call "%VCVARS%" x86

cl.exe /LD /W3 /MT /O2 ^
    /D WIN32 /D _WINDOWS ^
    /I "%SRC%" ^
    "%SRC%\nvda_menu.cpp" ^
    /link /OUT:nvda_menu.dll ^
    kernel32.lib user32.lib

if %ERRORLEVEL% neq 0 (
    echo Build FAILED.
    exit /b 1
)

echo Build succeeded: nvda_menu.dll

echo Deploying to game folder...
copy /Y nvda_menu.dll "%GAME%\nvda_menu.dll" >nul
if %ERRORLEVEL% neq 0 (
    echo WARNING: Deploy failed - copy nvda_menu.dll manually to:
    echo   %GAME%\nvda_menu.dll
)

echo Done.

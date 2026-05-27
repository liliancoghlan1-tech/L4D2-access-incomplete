@echo off
setlocal

set MSVC_ROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat

set SM_PUB=..\extension\sdk\sm\public
set SP_INC=..\extension\sdk\sp\include
set AMTL=..\extension\sdk\amtl\amtl
set AMTL_ROOT=..\extension\sdk\amtl
set SRC=..\extension\src

call "%VCVARS%" x86

cl.exe /LD /W3 /MT /O2 ^
    /D WIN32 /D _WINDOWS ^
    /I "%SRC%" ^
    /I "%SM_PUB%" ^
    /I "%SP_INC%" ^
    /I "%AMTL%" ^
    /I "%AMTL_ROOT%" ^
    "%SRC%\extension.cpp" ^
    "%SM_PUB%\smsdk_ext.cpp" ^
    /link /OUT:nvda.ext.dll ^
    kernel32.lib user32.lib

if %ERRORLEVEL% == 0 (
    echo Build succeeded: nvda.ext.dll
) else (
    echo Build FAILED.
    exit /b 1
)

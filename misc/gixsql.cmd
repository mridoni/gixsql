@echo off
rem setlocal

set "bin_dir=%~dp0"
set "GIXPP=%bin_dir%\gixpp.exe"

if "%1" == "" (
   echo Usage: gixsql input-file output-file [options]...
   echo Try 'gixsql --help' for more information.
   exit /b 1
)

set "arg=%1"
if "%arg:~0,1%"=="/"    set "arg=-%arg:~1%"
if "%arg%"=="--version" set "arg=-V"
if "%arg:~0,3%"=="--h"  set "arg=-h"
if "%2" == "" (
   if "%arg%" == "-V" (
       echo GixSQL wrapper script to gixpp
   ) else if "%arg%" == "-h" (
       echo GixSQL wrapper script to gixpp
       echo.
       echo Usage: gixsql input-file output-file [options]...
       echo.
       echo Help for gixpp, which documents additional options to use, follows - see "ESQL:".
       echo.
   )
   "%GIXPP%" -e "%arg%"
) else (
   "%GIXPP%" -e -I "%bin_dir%\..\lib\copy" -i "%1" -o "%2" %3 %4 %5 %6 %7 %8 %9
)

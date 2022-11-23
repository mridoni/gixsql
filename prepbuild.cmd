@echo off

:: If /E is specified on the command line we take the following variables from the current environment
IF "%1" == "/E" goto :fromenv

:: Check these variables and in case adjust them depending on your environment
	set HOST_PLATFORM=x64
	set VCPKG_ROOT=C:\VCPKG
:: Check these variables (end)

:: These indicates the current version of GixSQL included in Gix-IDE
	set GIXSQLMAJ=1
	set GIXSQLMIN=0
	set GIXSQLREL=19dev1
:: These indicates the current version of GixSQL included in Gix-IDE (end)

:fromenv

SET MYPATH=%~dp0
set SCRIPT_DIR=%MYPATH:~0,-1%

echo Configuring version (%GIXSQLMAJ%.%GIXSQLMIN%.%GIXSQLREL%) in header file for GixSQL
echo #define VERSION "%GIXSQLMAJ%.%GIXSQLMIN%.%GIXSQLREL%" > %SCRIPT_DIR%\gixsql\config.h

echo Done
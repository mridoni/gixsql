@echo off

setlocal enabledelayedexpansion

SET MYPATH=%~dp0
set SCRIPT_DIR=%MYPATH:~0,-1%

mkdir %SCRIPT_DIR%\doc
mkdir %SCRIPT_DIR%\examples

copy README.md %SCRIPT_DIR%\doc
copy TESTING.md %SCRIPT_DIR%\doc
copy %SCRIPT_DIR%\gixsql-tests-nunit\data\*.cbl %SCRIPT_DIR%\examples
copy %SCRIPT_DIR%\gixsql-tests-nunit\data\*.cpy %SCRIPT_DIR%\examples
copy %SCRIPT_DIR%\gixsql-tests-nunit\data\*.sql %SCRIPT_DIR%\examples

del /Q %SCRIPT_DIR%\examples\README
echo Example files for GixSQL >> %SCRIPT_DIR%\examples\README
echo (c) 2022 Marco Ridoni >> %SCRIPT_DIR%\examples\README
echo License: GPL/LGPL 3.0 >> %SCRIPT_DIR%\examples\README
echo ========================== >> %SCRIPT_DIR%\examples\README
echo. >> %SCRIPT_DIR%\examples\README
echo These example programs and SQL scripts are part of the GixSQL test suite >> %SCRIPT_DIR%\examples\README
echo that is available for distribution at https://github.com/mridoni/gixsql >> %SCRIPT_DIR%\examples\README

del /Q %SCRIPT_DIR%\extra_files.mk
echo DOC_FILES = doc/README >> %SCRIPT_DIR%\extra_files.mk
echo. >> %SCRIPT_DIR%\extra_files.mk
echo EXAMPLES_FILES = \  >> %SCRIPT_DIR%\extra_files.mk

for %%f in (%SCRIPT_DIR%\examples\*.*) do (
  set /p val=<%%f
  echo         examples\%%~nf \\ >> %SCRIPT_DIR%/extra_files.mk 
)

:: spaces here work as separators
echo         examples\README >> %SCRIPT_DIR%/extra_files.mk



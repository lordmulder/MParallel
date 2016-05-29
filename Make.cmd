@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

REM ///////////////////////////////////////////////////////////////////////////
REM // Setup environment
REM ///////////////////////////////////////////////////////////////////////////

REM Windows specific
set "MSVC_PATH=C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC"
set "PDOC_PATH=C:\Program Files (x86)\Pandoc"
set "GIT2_PATH=C:\Program Files\Git\bin"
set "UPX3_PATH=C:\Program Files (x86)\UPX"

REM ///////////////////////////////////////////////////////////////////////////
REM // Check paths
REM ///////////////////////////////////////////////////////////////////////////

if not exist "%MSVC_PATH%\vcvarsall.bat" (
	"%~dp0\etc\cecho.exe" RED "\nMSVC not found.\n%MSVC_PATH:\=\\%\\vcvarsall.bat\n"
	pause & goto:eof
)

if not exist "%PDOC_PATH%\pandoc.exe" (
	"%~dp0\etc\cecho.exe" RED "\nPandoc not found.\n%PDOC_PATH:\=\\%\\pandoc.exe\n"
	pause & goto:eof
)

if not exist "%GIT2_PATH%\git.exe" (
	"%~dp0\etc\cecho.exe" RED "\nGIT not found.\n%GIT2_PATH:\=\\%\\git.exe\n"
	pause & goto:eof
)

REM ///////////////////////////////////////////////////////////////////////////
REM // Get current date and time (in ISO format)
REM ///////////////////////////////////////////////////////////////////////////

set "ISO_DATE="
set "ISO_TIME="
for /F "tokens=1,2 delims=:" %%a in ('"%~dp0\etc\date.exe" +ISODATE:%%Y-%%m-%%d') do (
	if "%%a"=="ISODATE" set "ISO_DATE=%%b"
)
for /F "tokens=1,2,3,4 delims=:" %%a in ('"%~dp0\etc\date.exe" +ISOTIME:%%T') do (
	if "%%a"=="ISOTIME" set "ISO_TIME=%%b:%%c:%%d"
)


REM ///////////////////////////////////////////////////////////////////////////
REM // Clean up temp files
REM ///////////////////////////////////////////////////////////////////////////

"%~dp0\etc\cecho.exe" YELLOW "\n========[ CLEAN UP ]========\n"

for %%i in (bin,obj) do (
	del /Q /S /F "%~dp0\%%i\*.*"  2> NUL
)


REM ///////////////////////////////////////////////////////////////////////////
REM // Build the binaries
REM ///////////////////////////////////////////////////////////////////////////

"%~dp0\etc\cecho.exe" YELLOW "\n========[ COMPILE ]========"

call "%MSVC_PATH%\vcvarsall.bat"

MSBuild.exe /property:Platform=x86 /property:Configuration=Release /target:Clean   "%~dp0\MParallel.sln"
if not "!ERRORLEVEL!"=="0" goto BuildHasFailed
MSBuild.exe /property:Platform=x86 /property:Configuration=Release /target:Rebuild "%~dp0\MParallel.sln"
if not "!ERRORLEVEL!"=="0" goto BuildHasFailed

"%UPX3_PATH%\upx.exe" --best "%~dp0\bin\Win32\Release\MParallel.exe"


REM ///////////////////////////////////////////////////////////////////////////
REM // Generate Docs
REM ///////////////////////////////////////////////////////////////////////////

"%PDOC_PATH%\pandoc.exe" --from markdown --to html5 --toc -N --standalone -H "%~dp0\etc\css\style.inc" --output "%~dp0\README.html" "%~dp0\README.md"
if not "%ERRORLEVEL%"=="0" goto BuildHasFailed


REM ///////////////////////////////////////////////////////////////////////////
REM // Generate output name
REM ///////////////////////////////////////////////////////////////////////////

mkdir "%~dp0\out" 2> NUL

set COUNTER=
set REVISON=

:GenerateOutfileNameNext
set "OUT_PATH_BIN=%~dp0\out\mparallel.%ISO_DATE%%REVISON%.bin-win32.zip"
set "OUT_PATH_SRC=%~dp0\out\mparallel.%ISO_DATE%%REVISON%.src.tgz"

set /a COUNTER=COUNTER+1
set REVISON=.update-%COUNTER%

if exist "%OUT_PATH_BIN%" goto GenerateOutfileNameNext
if exist "%OUT_PATH_SRC%" goto GenerateOutfileNameNext


REM ///////////////////////////////////////////////////////////////////////////
REM // Build ZIP package
REM ///////////////////////////////////////////////////////////////////////////

"%~dp0\etc\cecho.exe" YELLOW "\n========[ PACKAGING ]========\n"

"%~dp0\etc\zip.exe" -j -9 -z "%OUT_PATH_BIN%" "%~dp0\bin\Win32\Release\MParallel.exe" "%~dp0\README.html" "%~dp0\COPYING.txt" < "%~dp0\COPYING.txt"
"%GIT2_PATH%\git.exe" archive --format tar.gz -9 --verbose --output "%OUT_PATH_SRC%" HEAD


REM ///////////////////////////////////////////////////////////////////////////
REM // Completed
REM ///////////////////////////////////////////////////////////////////////////

"%~dp0\etc\cecho.exe" GREEN "\nBUILD COMPLETED.\n"
pause
goto:eof


REM ///////////////////////////////////////////////////////////////////////////
REM // Failed
REM ///////////////////////////////////////////////////////////////////////////

:BuildHasFailed
"%~dp0\etc\cecho.exe" RED "\nBUILD HAS FAILED.\n"
pause
goto:eof

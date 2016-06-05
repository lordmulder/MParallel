@echo off
REM ///////////////////////////////////////////////////////////////////////////////
REM // MParallel - Parallel Batch Processor
REM // Copyright (c) 2016 LoRd_MuldeR <mulder2@gmx.de>. Some rights reserved.
REM //
REM // This program is free software; you can redistribute it and/or
REM // modify it under the terms of the GNU General Public License
REM // as published by the Free Software Foundation; either version 2
REM // of the License, or (at your option) any later version.
REM //
REM // This program is distributed in the hope that it will be useful,
REM // but WITHOUT ANY WARRANTY; without even the implied warranty of
REM // MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
REM // GNU General Public License for more details.
REM //
REM // You should have received a copy of the GNU General Public License
REM // along with this program; if not, write to the Free Software
REM // Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
REM //
REM // http://www.gnu.org/licenses/gpl-2.0.txt
REM ///////////////////////////////////////////////////////////////////////////////

REM Set proper Codepage so that 'dir' will produce the correct outputs
chcp 65001

REM Set path to the directory containing our input files
set "SOURCE_PATH=E:\PiCz\sCrEeNsHoTz"

REM Set path to ImageMagick
set "MAGICK_PATH=C:\Program Files\ImageMagick-7.0.1-Q16"

REM Detect path to the MParallel executable
set "MPARALLEL32=%~dp0\bin\v100\win32\Debug\MParallel.exe"

REM Add the ImageMagick path to PATH, because otherwise it will fail to load its DLL's
set "PATH=%MAGICK_PATH%;%PATH%"

REM Add VLD libraries to the path
set "PATH=%~dp0\etc\vld\bin\Win32;%PATH%"

REM Initialize the output directory, clear, if already exists
cd /d "%~dp0"
rmdir /S /Q "%~dp0\tmp" 2> NUL
mkdir "%~dp0\tmp"

REM Make sure VLD is enabled
for %%i in (%MPARALLEL32%) do (
	echo [Options]>  "%%~di%%~pi\vld.ini"
	echo VLD = on>> "%%~di%%~pi\vld.ini"
)

REM Finally, convert all image files, IN PARALLEL !!!
set "PATTERN=\"%MAGICK_PATH%\convert.exe\" \"%SOURCE_PATH%\{{0}}\" -resize 50%% \"%~dp0\tmp\{{0:N}}.png\""
dir /B "%SOURCE_PATH%\*.jpg" | "%MPARALLEL32%" --stdin --no-split-lines --logfile="%~dp0\tmp\~logfile.txt" --pattern="%PATTERN%"

REM Prevent console window from closing
pause

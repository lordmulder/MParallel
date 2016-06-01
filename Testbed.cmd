@echo off
chcp 65001

set "MAGICK_PATH=C:\Program Files\ImageMagick-7.0.1-Q16"
set "SOURCE_PATH=E:\PiCz\sCrEeNsHoTz"

cd /d "%~dp0"
rmdir /S /Q "%~dp0\tmp"
mkdir "%~dp0\tmp"

set "PATH=%MAGICK_PATH%;%PATH%"
dir /B "%SOURCE_PATH%\*.jpg" > "%~dp0\tmp\~files.txt"
dir /B "%SOURCE_PATH%\*.jpg" | "%~dp0\bin\Win32\Debug\MParallel.exe" --stdin --logfile="%~dp0\tmp\~logfile.txt" --pattern="\"%MAGICK_PATH%\convert.exe\" \"%SOURCE_PATH%\{{0}}\" -resize 50%% \"%~dp0\tmp\{{0:N}}.png\""

pause

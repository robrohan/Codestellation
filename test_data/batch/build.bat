@echo off
REM Entry point: every batch dependency style in one script.
setlocal enabledelayedexpansion

REM call a script                     -> lib\env.bat
call lib\env.bat

REM relative to this script via %~dp0 -> scripts\compile.cmd
call "%~dp0scripts\compile.cmd" release

REM a label in this file: not a dependency
call :cleanup

REM run a script directly             -> scripts\test.bat
scripts\test.bat

REM batch -> PowerShell               -> tools\package.ps1
powershell -ExecutionPolicy Bypass -File "%~dp0tools\package.ps1"

REM start another script              -> scripts\notify.bat
start "" "%~dp0scripts\notify.bat"

goto :eof

:cleanup
del /q out\*.tmp
exit /b 0

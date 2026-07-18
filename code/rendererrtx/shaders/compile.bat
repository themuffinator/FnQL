@echo off
setlocal

set SCRIPT_DIR=%~dp0

where py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
	py -3 "%SCRIPT_DIR%build_shaders.py" %*
) else (
	python "%SCRIPT_DIR%build_shaders.py" %*
)

if %ERRORLEVEL% NEQ 0 (
	exit /b %ERRORLEVEL%
)

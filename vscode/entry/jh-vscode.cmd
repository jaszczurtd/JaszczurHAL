@echo off
setlocal DisableDelayedExpansion

set "JH_ENTRY_DIR=%~dp0"
set "JH_ENTRY_PY=%JH_ENTRY_DIR%jh_vscode.py"
set "JH_MANAGED_PYTHON=%JH_ENTRY_DIR%..\..\.build\windows\venv\Scripts\python.exe"

if defined JH_VSCODE_PYTHON goto check_explicit

if exist "%JH_MANAGED_PYTHON%" (
    "%JH_MANAGED_PYTHON%" -c "from serial.tools import list_ports" >nul 2>&1
    if not errorlevel 1 (
        set "JH_SELECTED_PYTHON=%JH_MANAGED_PYTHON%"
        goto run_path
    )
)

where py >nul 2>&1
if not errorlevel 1 (
    py -3 -c "from serial.tools import list_ports" >nul 2>&1
    if not errorlevel 1 goto run_py
)

where python >nul 2>&1
if not errorlevel 1 (
    python -c "from serial.tools import list_ports" >nul 2>&1
    if not errorlevel 1 goto run_python
)

1>&2 echo error: Windows host setup is incomplete; Python 3 with pyserial was not found.
1>&2 echo Set JH_VSCODE_PYTHON or prepare the managed JaszczurHAL environment.
exit /b 8

:check_explicit
"%JH_VSCODE_PYTHON%" -c "from serial.tools import list_ports" >nul 2>&1
if errorlevel 1 (
    1>&2 echo error: JH_VSCODE_PYTHON does not provide Python 3 with pyserial: %JH_VSCODE_PYTHON%
    exit /b 8
)
set "JH_SELECTED_PYTHON=%JH_VSCODE_PYTHON%"

:run_path
"%JH_SELECTED_PYTHON%" "%JH_ENTRY_PY%" %*
exit /b %ERRORLEVEL%

:run_py
py -3 "%JH_ENTRY_PY%" %*
exit /b %ERRORLEVEL%

:run_python
python "%JH_ENTRY_PY%" %*
exit /b %ERRORLEVEL%

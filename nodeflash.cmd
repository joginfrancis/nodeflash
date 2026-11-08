@echo off
rem nodeflash launcher for Windows: creates a private Python environment on first use.
setlocal
set HERE=%~dp0
if not exist "%HERE%.venv\Scripts\python.exe" (
  echo First run: setting up nodeflash in %HERE%.venv ...
  python -m venv "%HERE%.venv" || exit /b 1
  "%HERE%.venv\Scripts\python.exe" -m pip install --quiet --upgrade pip
  "%HERE%.venv\Scripts\python.exe" -m pip install --quiet -e "%HERE%." || exit /b 1
)
call "%HERE%.venv\Scripts\python.exe" -m nodeflash %*

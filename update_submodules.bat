@echo off
py -3 "%~dp0tools\setup_repo.py" %*
exit /b %errorlevel%

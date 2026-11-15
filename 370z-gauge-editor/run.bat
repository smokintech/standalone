@echo off
rem Start the Oil Gauge layout editor (opens in your browser).
cd /d "%~dp0"
py editor.py %* || python editor.py %*

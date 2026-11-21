@echo off
rem Runs DAW Plugin Capture from source, installing its Python dependencies on first run.
cd /d "%~dp0"
where pythonw >nul 2>nul || (echo Python 3.9+ is required: https://www.python.org/downloads/ & pause & exit /b 1)
python -c "import obsws_python" 2>nul || (echo Installing dependencies... & python -m pip install -r requirements.txt)
start "" pythonw daw_plugin_capture.pyw

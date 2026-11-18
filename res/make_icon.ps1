# Rebuild the app icon from the approved docs\logo.png file.
# Requires Python 3 and Pillow (python -m pip install Pillow).
$script = Join-Path $PSScriptRoot "make_icon.py"
& python $script
if ($LASTEXITCODE -ne 0) { throw "App icon generation failed; install Python and Pillow." }

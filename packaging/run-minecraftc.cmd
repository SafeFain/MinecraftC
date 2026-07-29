@echo off
setlocal
"%~dp0minecraftc.exe" %*
if errorlevel 1 (
  echo.
  echo MinecraftC exited with an error. See minecraftc.log in the user data directory.
  pause
)

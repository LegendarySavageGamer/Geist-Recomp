@echo off
title Geist Online Relay
rem Starts ONLY the geist-online-relay container (compose project "geist").
rem Your other projects' containers are not touched.
cd /d "%~dp0"

where docker >nul 2>&1 || (echo Docker is not installed or not on PATH. & pause & exit /b 1)
docker info >nul 2>&1 || (echo Docker Desktop is not running. Start it, wait for "Engine running", then run this again. & pause & exit /b 1)

if not exist ".env" (
  copy ".env.example" ".env" >nul
  echo Created .env from .env.example
)

rem Warn if something other than this relay already holds UDP 47720.
for /f "tokens=*" %%L in ('docker ps --format "{{.Names}} {{.Ports}}" ^| findstr ":47720->" ^| findstr /v "geist-online-relay"') do (
  echo WARNING: another container already uses port 47720: %%L
  echo Change GEIST_PUBLIC_PORT in .env or stop that container, then run this again.
  pause & exit /b 1
)

echo Building and starting geist-online-relay ...
docker compose -p geist up -d --build || (echo Failed to start. & pause & exit /b 1)

echo.
docker ps --filter "name=geist-online-relay" --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
echo.
echo Last log lines (look for: msg=listening addr=:47720):
docker logs --tail 10 geist-online-relay
echo.
echo The relay keeps running in the background and restarts with Docker Desktop.
echo Live logs:  docker logs -f geist-online-relay      Stop:  stop_relay.bat
pause

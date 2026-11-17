@echo off
rem Stops ONLY the geist-online-relay container.
cd /d "%~dp0"
docker compose -p geist down
docker ps --filter "name=geist-online-relay" --format "{{.Names}}" | findstr geist-online-relay >nul && (echo Still running? Check Docker Desktop.) || echo geist-online-relay stopped.
pause

@echo off
title NovaMP Dedicated Server
cd /d "%~dp0"
echo Starting NovaMP Dedicated Server...
novaMP-server.exe ServerConfig.toml
pause

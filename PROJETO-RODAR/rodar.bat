@echo off
rem Windows: de dois cliques neste arquivo (roda a frente "basico"),
rem ou abra o Prompt de Comando nesta pasta e digite:  rodar.bat tamanho
rem Frentes: basico tamanho buffer threads leitura cifra
set FRENTE=%1
if "%FRENTE%"=="" set FRENTE=basico
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0rodar.ps1" -Frente %FRENTE%
pause

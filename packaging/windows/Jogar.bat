@echo off
rem Abre o port do Head Over Heels usando os dados do jogo original.
rem Se mudar a pasta do jogo, ajuste o caminho abaixo (ou copie hoh.exe e SDL2.dll para a pasta do jogo).
start "" "%~dp0hoh.exe" --data "F:\headover\HeadOverHeels"

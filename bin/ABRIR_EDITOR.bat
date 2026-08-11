@echo off
REM Abre o Ghost Map Editor.
REM
REM  - sem argumento: abre o hole da linha "gbin=" do editor.cfg
REM    (se estiver vazia, use Ctrl+O ou arraste um .gbin pra janela)
REM  - arrastando um .gbin em cima deste .bat: abre esse hole
REM
REM O "cd" aqui e obrigatorio: o exe le o editor.cfg da pasta atual.
cd /d "%~dp0"
start "" "GhostMapEditor.exe" %*

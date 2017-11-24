@echo off
for %%s in (Img\\*.bmp) do FVPCompress.exe "%%s" "graph.bin"
@echo Finished Compressing images
@echo on
@echo OK!
pause
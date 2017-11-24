FVPCompress ver4.0:
大大简化了之前复杂的操作。

用法很简单，将需要压回去的图片（反转好的bmp）放在Img文件夹里。然后把原始封包如graph.bin
放在本目录下。
然后执行run.bat即可。F社需要修改的图片一般都在graph.bin封包中，
如果不在graph.bin中，把下面的"graph.bin" 改为 目标的 "xxx.bin"就行了




@echo off
for %%s in (Img\\*.bmp) do FVPCompress.exe "%%s" "graph.bin"
@echo Finished Compressing images
@echo on
@echo OK!
pause


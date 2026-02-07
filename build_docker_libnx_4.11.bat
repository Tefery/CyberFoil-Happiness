@echo off
setlocal

set IMAGE=devkitpro/devkita64:20260202
set WORKDIR=%~dp0

docker run --rm -v "%WORKDIR%:/workspace" -w /workspace %IMAGE% sh -lc "make -C include/Plutonium -f Makefile clean && make clean && make -j 16"

endlocal

# ShinkuToolset
translation toolset for FAVORITE's games (FVP Engine)

## Warning
⚠️⚠️⚠️ **For translation scenarios, please use the following tools instead.** ⚠️⚠️⚠️
* [disassembler](https://github.com/xmoezzz/rfvp/tree/main/disassembler)
* [assembler](https://github.com/xmoezzz/rfvp/tree/main/assembler)
* This assembler not only restores translated strings back into the file, but also performs complete instruction assembly operations. If you're not familiar with FVP instructions, please refrain from modifying those instructions and their corresponding parameters casually.
* This project is part of RFVP, which aims to achieve an open-source, cross-platform engine built with Rust through reverse engineering to analyze the behavior of the original engine.


## FVPCompress
Pack bmp image to FVP's texture.

## FVPDeasm
Disassemble *.hcb bytecode.

## FVPDecompiler
Decompile *.hcb bytecode into c-like pseudocode.

## FVPTextExtract
Extract text from *.hcb bytecode(for translation project).

## UniversalPatch
Patch sample, you should implement some API functions before compiling (It's very easy to implement).
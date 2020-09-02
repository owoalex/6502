#!/bin/bash
if make; then
    #rm "./src/intermediate.asm"
    #cp "./src/testA.asm" "./src/intermediate.asm"
    #vasm6502_oldstyle "./src/intermediate.asm" -chklabels -nocase -dotdir -Dvasm=1 -L "./src/intermediate.asm" -DBuildBBC=1 -Fbin -o "./build/test.rom"
    #python3 "genVirtEPROM.py"
    
    build/emu -p "./Os12.rom" -s "./Basic2.rom" -P 0000052e
    #build/emu -f "./build/testExpanded.rom"
else
    echo "BUILD FAILED"
fi

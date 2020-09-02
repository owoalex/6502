#!/bin/bash
vasm6502_oldstyle "./src/testA.asm" -chklabels -nocase -Dvasm=1 -L "./src/testA.asm" -DBuildBBC=1 -Fbin -o "./fullrom.rom"

#build/emu -p "./Os12.rom" -s "./Basic2.rom"

build/emu -f "./fullrom.rom"

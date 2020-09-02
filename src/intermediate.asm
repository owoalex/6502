F00:0001       
F00:0002       	.org $8000			;Start of our program code.
F00:0003       
F00:0004       	;Load in the address of the Message into the zero page
F00:0005       	lda #>HelloWorld
               S01:FFFFFFFFFFFF8000:  A9 80
F00:0006       	sta $21				;H Byte
               S01:FFFFFFFFFFFF8002:  85 21
F00:0007       	lda #<HelloWorld
               S01:FFFFFFFFFFFF8004:  A9 19
F00:0008       	sta $20				;L Byte
               S01:FFFFFFFFFFFF8006:  85 20
F00:0009       	
F00:0010       	jsr PrintStr		;Show to the screen
               S01:FFFFFFFFFFFF8008:  20 0C 80
F00:0011       
F00:0012       	rts					;Return to basic
               S01:FFFFFFFFFFFF800B:  60
F00:0013       
F00:0014       
F00:0015       PrintStr:
F00:0016       	ldy #0				;Set Y to zero
               S01:FFFFFFFFFFFF800C:  A0 00
F00:0017       PrintStr_again:
F00:0018       	lda ($20),y			;Load a character from addr in $20+Y 
               S01:FFFFFFFFFFFF800E:  B1 20
F00:0019       	
F00:0020       	cmp #255			;If we got 255, we're done
               S01:FFFFFFFFFFFF8010:  C9 FF
F00:0021       	beq PrintStr_Done
               S01:FFFFFFFFFFFF8012:  F0 04
F00:0022       	
F00:0023       	iny					;Inc Y and repeat
               S01:FFFFFFFFFFFF8014:  C8
F00:0024       	jmp PrintStr_again
               S01:FFFFFFFFFFFF8015:  4C 0E 80
F00:0025       PrintStr_Done:
F00:0026       	rts	
               S01:FFFFFFFFFFFF8018:  60
F00:0027       
F00:0028       HelloWorld:				;255 terminated string
F00:0029       	db "Hello World",255
               S01:FFFFFFFFFFFF8019:  48 65 6C 6C 6F 20 57 6F 72 6C 64
               S01:FFFFFFFFFFFF8024:  FF
F00:0030       	
F00:0031       	
F00:0032       	
F00:0033       


Sections:
S01  seg8000


Sources:
F00  intermediate.asm


Symbols:
PrintStr_Done EXPR(-32744=0x8018) ABS 
PrintStr_again EXPR(-32754=0x800e) ABS 
PrintStr EXPR(-32756=0x800c) ABS 
HelloWorld EXPR(-32743=0x8019) ABS 
__RPTCNT EXPR(-1=0xffff) INTERNAL 
__VASM EXPR(0=0x0) INTERNAL 
BuildBBC EXPR(1=0x1) UNUSED 
vasm EXPR(1=0x1) UNUSED 

There have been no errors.


	.org $8000			;Start of our program code.

	;Load in the address of the Message into the zero page
	lda #>HelloWorld
	sta $21				;H Byte
	lda #<HelloWorld
	sta $20				;L Byte
	
	jsr PrintStr		;Show to the screen

	rts					;Return to basic


PrintStr:
	ldy #0				;Set Y to zero
PrintStr_again:
	lda ($20),y			;Load a character from addr in $20+Y 
	
	cmp #255			;If we got 255, we're done
	beq PrintStr_Done
	
	iny					;Inc Y and repeat
	jmp PrintStr_again
PrintStr_Done:
	rts	

HelloWorld:				;255 terminated string
	db "Hello World",255
	
	
	

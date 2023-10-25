; 65c02 JSR/RTS/SBC/BEQ
		.org $2000
		;.verbose
		CLC
		LDA #$00
		JSR skip
		SEC
		SBC #$03
		BEQ back
		JMP fail
skip	LDA #$03
		RTS
fail	BRK    ; JSR Failed!
; now test setting some address on the stack and call RTS
jump	LDA #>pass
		PHA
		LDA #<pass
		DEC
		PHA
		RTS
		BRK
back	BRA jump
		BRK
rts_on  NOP
pass	BRK

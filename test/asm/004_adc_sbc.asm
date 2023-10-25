; Test ADC/SBC, also BCD, eventual
			.org $2000
			;.verbose
			sec
			lda #$50
			sbc #$f0
			beq fail
			bcs fail
			bvs fail
			cmp #$60
			bne fail

			sec
			lda #$50
			sbc #$b0
			bpl fail2
			bvc fail2
			cmp #$a0
			bne fail2

			sec
			lda #$50
			sbc #$30
			bcc fail3
			cmp #$20
			bne fail3
pass		brk

fail		brk ; Failed basic HEX substraction
fail2		brk ; N V test
fail3		brk ; C test

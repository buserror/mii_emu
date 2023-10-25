; Test for addressing modes
		.org $10
		;.verbose

imm		lda #$aa
zp_rel	ldx $0010	; converted to ZP load by asm
		cpx #$a9 	; lda !
		bne fail1
		lda $11
		tax
		cpx #$aa
		bne fail2
		ldx #$10
zp_x	ldy $0,x	; converted to ZP load by asm
		cpy #$a9
		bne fail3
		ldy #$01
abs_y	ldx $10,y
		cpx #$aa
		bne fail4
abs_x	lda #$de
		sta $101
		lda #$ad
		lda $100
		ldx #$1
		ldy $100,x
		cpy #$de
		bne fail5
; Now lets go test the indirect ones
dst_ad	= $3000
ind_x	lda #$a9
		sta dst_ad
		stz $f2
		lda #$30
		sta $f3
		ldx #$2
		lda ($f0,x) ; sould read $00 $00 from $f2,$f3
		tay
		cpy #$a9
		bne fail6
		bra pass
fail1	brk 		; Basic ABS load failed
fail2	brk 		; Basic $xx ZP load failed
fail3	brk 		; $xx,x ZP load failed
fail4	brk 		; $xxxx,y ZP load failed
fail5	brk 		; ($xxxx) load failed
fail6	brk 		; ($xx,x) load failed
pass	brk


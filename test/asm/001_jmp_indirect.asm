; 65c02 Indirect Indexed JMP ($xxx,X)
			.org	$2000
jump_ad 	= $3000
			;.verbose
			JMP		skip
ind			LDA 	#$99   ; $2003
			BRA		pass
skip:		LDA		#<ind  ; $2000
			LDY		#$02
			STA		jump_ad,Y
			INY
			LDA		#>ind
			STA		jump_ad,Y
			LDX		#$02
			JMP		($3000,X)   ; JMP -> $2003
fail		BRK    ; JMP Failed!
pass		BRK

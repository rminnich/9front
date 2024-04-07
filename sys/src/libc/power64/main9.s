TEXT	_main(SB), 1, $-8
	ADD	$-16, R1
	MOVD	$0, R0
	MOVD	$setSB(SB), R2
	MOVD	R3, _tos(SB)

	MOVD	$main(SB), R3
	MOVD	R0, LR
	MOVD	$_callmain(SB), R4
	MOVD	R4, CTR
	BR	(CTR)

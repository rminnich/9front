TEXT main(SB), 1, $8
	MOV	$setSB(SB), R3		/* load the SB */
	MOV	$boot(SB), R0
	JAL	R1, startboot(SB)

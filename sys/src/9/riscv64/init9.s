TEXT main(SB), 1, $48
	MOV	$setSB(SB), R3		/* load the SB */
	MOV	$boot(SB), R8		/* R8 = &boot (R0 is zero reg on riscv) */
	MOV	R8, 8(R2)		/* startboot arg0 (name): &boot */
	MOV	R8, 24(R2)		/* argv[0] = &boot */
	MOV	R0, 32(R2)		/* argv[1] = nil  (sentinel) */
	ADD	$24, R2, R9		/* R9 = &argv[0] */
	MOV	R9, 16(R2)		/* startboot arg1 (argv): &argv[0] */
	JAL	R1, startboot(SB)

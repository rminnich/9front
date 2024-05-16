/*  get variants */
TEXT agetl+0(SB),1,$0
	SYNC
	// See ISA 3.0B section B.2.3, "Safe Fetch"
	MOVWZ	0(R3), R3
	CMPW	R3, R3, CR7
	BC	4, 30, 1(PC) // bne- cr7,0x4
	ISYNC
	MOVW	R3, ret+8(FP)
	RETURN

TEXT agetv+0(SB),1,$0
TEXT agetp+0(SB),1,$0
	SYNC
	// See ISA 3.0B section B.2.3, "Safe Fetch"
	MOVD	0(R3), R3
	CMP	R3, R3, CR7
	BC	4, 30, 1(PC) // bne- cr7,0x4
	ISYNC
	MOVD	R3, ret+8(FP)
	RETURN

/*  set variants */
TEXT asetl+0(SB),1,$0
	MOVW	val+8(FP), R4
	SYNC
	MOVW	R4, 0(R3)
	RETURN

TEXT asetv+0(SB),1,$0
TEXT asetp+0(SB),1,$0
	MOVD	val+8(FP), R4
	SYNC
	MOVD	R4, 0(R3)
	RETURN

/*  inc variants */
TEXT aincl+0(SB),1,$0
	MOVD	R3, R4
	MOVW	delta+8(FP), R5
	LWSYNC
	LWAR	(R4), R3
	ADD	R5, R3
	STWCCC	R3, (R4)
	BNE	-3(PC)
	MOVW	R3, ret+16(FP)
	RETURN

TEXT aincv+0(SB),1,$0
TEXT aincp+0(SB),1,$0
	MOVD	delta+8(FP), R5
	LWSYNC
	LDAR	(R3), R4
	ADD	R5, R4
	STDCCC	R4, (R3)
	BNE	-3(PC)
	MOVD	R4, ret+16(FP)
	RETURN

/*  cas variants */
TEXT acasl+0(SB),1,$0
	MOVWZ	old+8(FP), R4
	MOVWZ	new+12(FP), R5
	LWSYNC
casagain:
	LWAR	(R3), R6
	CMPW	R6, R4
	BNE	casfail
	STWCCC	R5, (R3)
	BNE	casagain
	MOVD	$1, R3
	LWSYNC
	MOVB	R3, ret+16(FP)
	RETURN
casfail:
	LWSYNC
	MOVB	R0, ret+16(FP)
	RETURN

TEXT acasv+0(SB),1,$0
TEXT acasp+0(SB),1,$0
	MOVD	old+8(FP), R4
	MOVD	new+16(FP), R5
	LWSYNC
cas64again:
	LDAR	(R3), R6
	CMP	R6, R4
	BNE	cas64fail
	STDCCC	R5, (R3)
	BNE	cas64again
	MOVD	$1, R3
	LWSYNC
	MOVB	R3, ret+24(FP)
	RETURN
cas64fail:
	LWSYNC
	MOVB	R0, ret+24(FP)
	RETURN

/* barriers */
TEXT coherence+0(SB),1,$0
	// LWSYNC is the "export" barrier recommended by Power ISA
	// v2.07 book II, appendix B.2.2.2.
	// LWSYNC is a load/load, load/store, and store/store barrier.
	LWSYNC
	RETURN

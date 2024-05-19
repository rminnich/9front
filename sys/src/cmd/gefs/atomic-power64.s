/*  get variants */
TEXT agetl+0(SB),1,$0
	SYNC
	// See ISA 3.0B section B.2.3, "Safe Fetch"
	MOVWZ	0(RARG), RARG
	CMPW	RARG, RARG, CR7
	BC	4, 30, 1(PC) // bne- cr7,0x4
	ISYNC
	RETURN

TEXT agetv+0(SB),1,$0
TEXT agetp+0(SB),1,$0
	SYNC
	// See ISA 3.0B section B.2.3, "Safe Fetch"
	MOVD	0(RARG), RARG
	CMP	RARG, RARG, CR7
	BC	4, 30, 1(PC) // bne- cr7,0x4
	ISYNC
	RETURN

/*  set variants */
TEXT asetl+0(SB),1,$0
	MOVW	val+8(FP), R4
	SYNC
	MOVW	R4, 0(RARG)
	RETURN

TEXT asetv+0(SB),1,$0
TEXT asetp+0(SB),1,$0
	MOVD	val+8(FP), R4
	SYNC
	MOVD	R4, 0(RARG)
	RETURN

/*  inc variants */
TEXT aincl+0(SB),1,$0
	MOVD	RARG, R4
	MOVW	delta+8(FP), R5
	LWSYNC
	LWAR	(R4), RARG
	ADD	R5, RARG
	STWCCC	RARG, (R4)
	BNE	-3(PC)
	RETURN

TEXT aincv+0(SB),1,$0
TEXT aincp+0(SB),1,$0
	MOVD	RARG, R4
	MOVD	delta+8(FP), R5
	LWSYNC
	LDAR	(R4), RARG
	ADD	R5, RARG
	STDCCC	RARG, (R4)
	BNE	-3(PC)
	RETURN

/*  cas variants */
TEXT acasl+0(SB),1,$0
	MOVWZ	old+8(FP), R4
	MOVWZ	new+16(FP), R5
	LWSYNC
casagain:
	LWAR	(RARG), R6
	CMPW	R6, R4
	BNE	casfail
	STWCCC	R5, (RARG)
	BNE	casagain
	MOVD	$1, RARG
	LWSYNC
	RETURN
casfail:
	LWSYNC
	AND	R0, RARG
	RETURN

TEXT acasv+0(SB),1,$0
TEXT acasp+0(SB),1,$0
	MOVD	old+8(FP), R4
	MOVD	new+16(FP), R5
	LWSYNC
cas64again:
	LDAR	(RARG), R6
	CMP	R6, R4
	BNE	cas64fail
	STDCCC	R5, (RARG)
	BNE	cas64again
	MOVD	$1, RARG
	LWSYNC
	RETURN
cas64fail:
	LWSYNC
	AND	R0, RARG
	RETURN

/* barriers */
TEXT coherence+0(SB),1,$0
	// LWSYNC is the "export" barrier recommended by Power ISA
	// v2.07 book II, appendix B.2.2.2.
	// LWSYNC is a load/load, load/store, and store/store barrier.
	LWSYNC
	RETURN

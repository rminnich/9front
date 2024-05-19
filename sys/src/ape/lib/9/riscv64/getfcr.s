#define FFLAGS		1
#define FRM		2
#define FCSR		3

TEXT	getfsr(SB), $0
	MOV	CSR(FCSR), RARG
	RET

TEXT	setfsr(SB), $0
	MOV	RARG, CSR(FCSR)
	RET

TEXT	getfcr(SB), $0
	MOV	CSR(FCSR), RARG
	RET

TEXT	setfcr(SB), $0
	MOV	RARG, CSR(FCSR)
	RET

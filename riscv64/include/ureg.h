struct Ureg
{
	union {
		uintptr	regs[32];		/* regs[n] is R(n), n!=0 */
		struct {
			uintptr	pc;		/* instead of r0 */
			/* the names r1 - r31 are just for libmach */
			union {
				uintptr	r1;
				uintptr	link;	/* r1 */
			};
			union {
				uintptr	r2;
				uintptr	sp;	/* r2 */
			};
			uintptr	r3;
			uintptr	r4;
			uintptr	r5;
			uintptr	r6;		/* up in kernel */
			uintptr	r7;		/* m in kernel */
			union{
				uintptr	r8;
				uintptr	arg;	/* r8 */
				uintptr	ret;
			};
			uintptr	r9;
			uintptr	r10;
			uintptr	r11;
			uintptr	r12;
			uintptr	r13;
			uintptr	r14;
			uintptr	r15;
			uintptr	r16;
			uintptr	r17;
			uintptr	r18;
			uintptr	r19;
			uintptr	r20;
			uintptr	r21;
			uintptr	r22;
			uintptr	r23;
			uintptr	r24;
			uintptr	r25;
			uintptr	r26;
			uintptr	r27;
			uintptr	r28;
			uintptr	r29;
			uintptr	r30;
			uintptr	r31;
		};
	};

	/* csrs: generally supervisor ones */
	uintptr	status;
	uintptr	ie;
	union {
		uintptr	cause;
		uintptr	type;
	};
	uintptr	tval;				/* faulting address */

	uintptr	curmode;
};

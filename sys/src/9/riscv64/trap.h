enum {
	Trapdebug	= 0,
	Probedebug	= 1,
	Intrdebug	= 0,
	Tryallcpus	= 0,
	TrapSpew	= 0,
	TrapOhShit  = 0,
	TrapSys		= 0,

	Ntimevec = 20,		/* number of time buckets for each intr */
	Ncauses = Ngintr + Nlintr + Nexc,	/* # of Vctls */
};
enum Faulttypes {
	Unknownflt, Exception, Localintr, Globalintr, Nfaulttypes,
};

enum {
	Read = 0,
	Write = 1,
};
long	agetl(long*);
vlong	agetv(vlong*);
void*	agetp(void**);

long	asetl(long*, long);
vlong	asetv(vlong*, vlong);
void*	asetp(void**, void*);

long	aincl(long*, long);
vlong	aincv(vlong*, vlong);

int	acasl(long*, long, long);
int	acasv(vlong*, vlong, vlong);
int	acasp(void**, void*, void*);

void	coherence(void);

void	attachdev(Dev*);
void	detachdev(Dev*);
void	work(void);
Hub*	newhub(char *, Dev*);
int	hname(char *);
void	assignhname(Dev *dev);
void	checkidle(void);
int	portfeature(Hub*, int, int, int);

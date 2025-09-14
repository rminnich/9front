void initworkrects(Rectangle*, int, Rectangle*);
void *emalloc(ulong);
void *erealloc(void*, ulong);
Memimage *eallocmemimage(Rectangle, ulong);
Memimage *ereadmemimage(int);
int ewritememimage(int, Memimage*);

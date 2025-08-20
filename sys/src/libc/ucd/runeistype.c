#include <u.h>
#include <libc.h>

#include "runeistypedata"

int
isspacerune(Rune c)
{
	if(c > Runemax)
		return 0;
	return (mergedlkup(c) & Lspace) == Lspace;
}

int
isalpharune(Rune c)
{
	if(c > Runemax)
		return 0;
	return (mergedlkup(c) & Lalpha) == Lalpha;
}

int
isdigitrune(Rune c)
{
	if(c > Runemax)
		return 0;
	return (mergedlkup(c) & Ldigit) == Ldigit;
}

int
isupperrune(Rune c)
{
	if(c > Runemax)
		return 0;
	return (mergedlkup(c) & Lupper) == Lupper;
}

int
islowerrune(Rune c)
{
	if(c > Runemax)
		return 0;
	return (mergedlkup(c) & Llower) == Llower;
}

int
istitlerune(Rune c)
{
	if(c > Runemax)
		return 0;
	return (mergedlkup(c) & Ltitle) == Ltitle;
}

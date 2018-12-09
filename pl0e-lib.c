//===------------------------------------------------===//
// The library for running the generated object file
// Usage: see readme file for details
//===-----------------------------------------------===//
#include <stdio.h>
#include <math.h>
void putf(double val)
{
	printf("%lf",val);
}
void puti(int val)
{
	printf("%d",val);
}
void getf(double* var)
{
	scanf("%lf",var);
}
void geti(int* var)
{
	scanf("%d",var);
}
void putch(char c)
{
	printf("%c",c);
}
int getrem(int a,int b)
{
	return a%b;
}

/*
 * sorted-list.c
 */

#include	<string.h>
#include	<stdio.h>
#include	"sorted-list.h"

int compareInts(void *p1, void *p2)
{
	int i1 = *(int*)p1;
	int i2 = *(int*)p2;

	return i1 - i2;
}

int compareDoubles(void *p1, void *p2)
{
	double d1 = *(double*)p1;
	double d2 = *(double*)p2;

	return (d1 < d2) ? -1 : ((d1 > d2) ? 1 : 0);
}

int compareStrings(void *p1, void *p2)
{
	char *s1 = p1;
	char *s2 = p2;

	return strcmp(s1, s2);
}

int main()
{
    SortedListPtr slp = SLCreate(compareInts);
    int v = 50;
    int w = 4;
    int x = 5;
    int y = 7;
    int z = 10;
    SLInsert(slp, (void*)&v);
    SLInsert(slp, (void*)&w);
    SLInsert(slp, (void*)&x);
    SLInsert(slp, (void*)&y);
    SLInsert(slp, (void*)&z);
    SortedListIteratorPtr iter = SLCreateIterator(slp);

    printf("hello: %d\n", *(int*)SLNextItem(iter));
    printf("hello: %d\n", *(int*)SLNextItem(iter));
    printf("hello: %d\n", *(int*)SLNextItem(iter));
    printf("hello: %d\n", *(int*)SLNextItem(iter));
    printf("hello: %d\n", *(int*)SLNextItem(iter));
    return 0;
}

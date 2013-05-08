// allocator_tests.cpp: определяет точку входа для консольного приложения.
//

#include "stdafx.h"
#include <windows.h>

#include "nedmalloc.h"
#if !defined(USE_NEDMALLOC_DLL)
#include "nedmalloc.c"
#endif

#define ALLOC_SIZE 58 + 4096
#define COUNT 1000000

double measure_time(void (*func)(), size_t count)
{
	LARGE_INTEGER timerFrequency, timerStart, timerStop;
	double t = 0.0;

	QueryPerformanceFrequency(&timerFrequency);
	QueryPerformanceCounter(&timerStart);
	for(size_t i = 0; i< count; i++)
	{
		func();
	}
	QueryPerformanceCounter(&timerStop);
	t = (double)( timerStop.QuadPart -	timerStart.QuadPart ) / timerFrequency.QuadPart;
	return t;
}


using namespace nedalloc;

nedpool *pool;

void malloc_test()
{
	void *p = malloc(ALLOC_SIZE);
	if(!p)
	{
		memset(p, 0, ALLOC_SIZE);
		free(p);
	}
}

void  nedmalloc_test()
{
	void *p = nedmalloc(ALLOC_SIZE);
	if(!p)
	{
		memset(p, 0, ALLOC_SIZE);
		nedfree(p);
	}
}

void  nedpmalloc_test()
{
	void *p = nedpmalloc(pool, ALLOC_SIZE);
	if(!p)
	{
		memset(p, 0, ALLOC_SIZE);
		nedpfree(pool, p);
	}
}



int _tmain(int argc, _TCHAR* argv[])
{
	double t1, t2, t3;
	pool = nedcreatepool(ALLOC_SIZE * 100, 1);

	t1 = measure_time(malloc_test, COUNT);
	t2 = measure_time(nedmalloc_test, COUNT);
	t3 = measure_time(nedpmalloc_test, COUNT);

	t1 = measure_time(malloc_test, COUNT);
	t2 = measure_time(nedmalloc_test, COUNT);
	t3 = measure_time(nedpmalloc_test, COUNT);

	printf("malloc     : %f sec\n", t1);
	printf("nedmalloc  : %f sec\n", t2);
	printf("nedpmalloc : %f sec\n", t3);

	neddestroypool(pool);
	system("pause");
	return 0;
}


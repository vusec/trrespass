#include "utils.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#define PAGE_BITS 12

#define FLAGS (MAP_PRIVATE | MAP_POPULATE | MAP_HUGETLB | (30<<MAP_HUGE_SHIFT))

char *get_rnd_addr(char *base, size_t m_size, size_t align)
{
	return (char *)((((uint64_t) base) + (rand() % m_size)) &
			(~((uint64_t) align - 1)));
}

int get_rnd_int(int min, int max)
{
	return rand() % (max + 1 - min) + min;
}

double mean(uint64_t * vals, size_t size)
{
	uint64_t avg = 0;
	for (size_t i = 0; i < size; i++) {
		avg += vals[i];
	}
	return ((double)avg) / size;
}

int gt(const void *a, const void *b)
{
	return (*(int *)a - *(int *)b);
}

uint64_t median(uint64_t * vals, size_t size)
{
	qsort(vals, size, sizeof(uint64_t), gt);
	return ((size % 2) ==
		0) ? vals[size / 2] : (vals[(size_t) size / 2] +
				       vals[((size_t) size / 2 + 1)]) / 2;
}

char *bit_string(uint64_t val)
{
	static char bit_str[256];
	char itoa_str[8];
	strcpy(bit_str, "");
	for (int shift = 0; shift < 64; shift++) {
		if ((val >> shift) & 1) {
			if (strcmp(bit_str, "") != 0) {
				strcat(bit_str, "+ ");
			}
			sprintf(itoa_str, "%d ", shift);
			strcat(bit_str, itoa_str);
		}
	}

	return bit_str;
}

char *int_2_bin(uint64_t val)
{
	static char bit_str[256];
	char itoa_str[8];
	strcpy(bit_str, "0b");
	for (int shift = 64 - __builtin_clzl(val); shift >= 0; --shift) {
		sprintf(itoa_str, "%d", (int)(val >> shift) & 1);
		strcat(bit_str, itoa_str);
	}

	return bit_str;
}

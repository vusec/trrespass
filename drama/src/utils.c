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

#include "utils.h"


//-----------------------------------------------
// 			Memory alloc

int alloc_buffer(mem_buff_t* mem) {
	if (mem->buffer != NULL) {
		fprintf(stderr, "[ERROR] - Memory already allocated\n");
	}

	uint64_t alloc_flags = MAP_PRIVATE | MAP_POPULATE | MAP_ANONYMOUS;

	mem->buffer = (char*) mmap(NULL, mem->size, PROT_READ | PROT_WRITE, alloc_flags, -1, 0);
	if (mem->buffer == MAP_FAILED) {
		perror("[ERROR] - mmap() failed");
		exit(1);
	}

	if (mem->flags & F_VERBOSE) {
		fprintf(stderr, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
		fprintf(stderr, "[ MEM ] - Buffer:      %p\n", mem->buffer);
		fprintf(stderr, "[ MEM ] - Size:        %ld\n", mem->size);
		fprintf(stderr, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
	}	
	return 0; 

}



int free_buffer(mem_buff_t* mem) {
	return munmap(mem->buffer, mem->size);
}





//-----------------------------------------------
// 				Helpers

double mean(uint64_t* vals, size_t size) {
	uint64_t avg = 0;
	for (size_t i = 0; i < size; i++) {
		avg += vals[i];
	}
	return ((double)avg) / size;
}

int gt(const void * a, const void * b) {
   return ( *(int*)a - *(int*)b );
}

uint64_t median(uint64_t* vals, size_t size) {
	qsort(vals, size, sizeof(uint64_t), gt);
	return ((size%2)==0) ? vals[size/2] : (vals[(size_t)size/2]+vals[((size_t)size/2+1)])/2;
}


char* bit_string(uint64_t val) {
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

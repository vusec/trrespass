#include "allocator.h"

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

int alloc_buffer(MemoryBuffer * mem)
{
	if (mem->buffer != NULL) {
		fprintf(stderr, "[ERROR] - Memory already allocated\n");
	}

	if (mem->align < _SC_PAGE_SIZE) {
		mem->align = 0;
	}

	uint64_t alloc_size = mem->align ? mem->size + mem->align : mem->size;
	uint64_t alloc_flags = MAP_PRIVATE | MAP_POPULATE;

	if (mem->flags & F_ALLOC_HUGE) {
		if (mem->fd == 0) {
			fprintf(stderr,
				"[ERROR] - Missing file descriptor to allocate hugepage\n");
			exit(1);
		}
		alloc_flags |=
		    (mem->flags & F_ALLOC_HUGE_1G) ? MAP_ANONYMOUS | MAP_HUGETLB
		    | (30 << MAP_HUGE_SHIFT)
		    : (mem->flags & F_ALLOC_HUGE_2M) ? MAP_ANONYMOUS |
		    MAP_HUGETLB | (21 << MAP_HUGE_SHIFT)
		    : MAP_ANONYMOUS;
	} else {
		mem->fd = -1;
		alloc_flags |= MAP_ANONYMOUS;
	}
	mem->buffer = (char *)mmap(NULL, mem->size, PROT_READ | PROT_WRITE,
				   alloc_flags, mem->fd, 0);
	if (mem->buffer == MAP_FAILED) {
		perror("[ERROR] - mmap() failed");
		exit(1);
	}
	if (mem->align) {
		size_t error = (uint64_t) mem->buffer % mem->align;
		size_t left = error ? mem->align - error : 0;
		munmap(mem->buffer, left);
		mem->buffer += left;
		assert((uint64_t) mem->buffer % mem->align == 0);
	}

	if (mem->flags & F_VERBOSE) {
		fprintf(stderr, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
		fprintf(stderr, "[ MEM ] - Buffer:      %p\n", mem->buffer);
		fprintf(stderr, "[ MEM ] - Size:        %ld\n", alloc_size);
		fprintf(stderr, "[ MEM ] - Alignment:   %ld\n", mem->align);
		fprintf(stderr, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
	}
	return 0;

}

int free_buffer(MemoryBuffer * mem)
{
	free(mem->physmap);
	return munmap(mem->buffer, mem->size);
}

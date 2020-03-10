#include "memory.h"
#include "utils.h"

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define DEF_RNG_LEN (8<<10)
#define DEBUG
#define DEBUG_LINE fprintf(stderr, "[DEBUG] - GOT HERE\n");

static physaddr_t base_phys = 0L;

uint64_t get_pfn(uint64_t entry)
{
	return ((entry) & 0x7fffffffffffffff);
}

physaddr_t get_physaddr(uint64_t v_addr, int pmap_fd)
{
	uint64_t entry;
	uint64_t offset = (v_addr / 4096) * sizeof(entry);
	uint64_t pfn;
	bool to_open = false;
	// assert(fd >= 0);
	if (pmap_fd == NOT_OPENED) {
		pmap_fd = open("/proc/self/pagemap", O_RDONLY);
		assert(pmap_fd >= 0);
		to_open = true;
	}
	// int rd = fread(&entry, sizeof(entry), 1 ,fp);
	int bytes_read = pread(pmap_fd, &entry, sizeof(entry), offset);

	assert(bytes_read == 8);
	assert(entry & (1ULL << 63));

	if (to_open) {
		close(pmap_fd);
	}

	pfn = get_pfn(entry);
	assert(pfn != 0);
	return (pfn << 12) | (v_addr & 4095);
}

int phys_cmp(const void *p1, const void *p2)
{
	return ((pte_t *) p1)->p_addr - ((pte_t *) p2)->p_addr;
}

// WARNING optimization works only with contiguous memory!!
void set_physmap(MemoryBuffer * mem)
{
	int l_size = mem->size / PAGE_SIZE;
	pte_t *physmap = (pte_t *) malloc(sizeof(pte_t) * l_size);
	int pmap_fd = open("/proc/self/pagemap", O_RDONLY);
	assert(pmap_fd >= 0);

	base_phys = get_physaddr((uint64_t) mem->buffer, pmap_fd);
	for (uint64_t tmp = (uint64_t) mem->buffer, idx = 0;
	     tmp < (uint64_t) mem->buffer + mem->size; tmp += PAGE_SIZE) {
		pte_t tmp_pte = { (char *)tmp, get_physaddr(tmp, pmap_fd) };
		physmap[idx] = tmp_pte;
		idx++;
	}

	qsort(physmap, mem->size / PAGE_SIZE, sizeof(pte_t), phys_cmp);
	close(pmap_fd);
	mem->physmap = physmap;
}

physaddr_t virt_2_phys(char *v_addr, MemoryBuffer * mem)
{
	for (int i = 0; i < mem->size / PAGE_SIZE; i++) {
		if (mem->physmap[i].v_addr ==
		    (char *)((uint64_t) v_addr & ~((uint64_t) (PAGE_SIZE - 1))))
		{

			return mem->physmap[i].
			    p_addr | ((uint64_t) v_addr &
				      ((uint64_t) PAGE_SIZE - 1));
		}
	}
	return (physaddr_t) NOT_FOUND;
}

char *phys_2_virt(physaddr_t p_addr, MemoryBuffer * mem)
{
	physaddr_t p_page = p_addr & ~(((uint64_t) PAGE_SIZE - 1));
	pte_t src_pte = {.v_addr = 0,.p_addr = p_page };
	pte_t *res_pte =
	    (pte_t *) bsearch(&src_pte, mem->physmap, mem->size / PAGE_SIZE,
			      sizeof(pte_t), phys_cmp);

	if (res_pte == NULL)
		return (char *)NOT_FOUND;

	return (char *)((uint64_t) res_pte->
			v_addr | ((uint64_t) p_addr &
				  (((uint64_t) PAGE_SIZE - 1))));
}

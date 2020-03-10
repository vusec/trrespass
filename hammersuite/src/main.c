#include "stdio.h"
// #include <x86intrin.h> /* for rdtsc, rdtscp, clflush */
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <getopt.h>
#include <errno.h>
#include <stdint.h>

#include "include/utils.h"
#include "include/types.h"
#include "include/allocator.h"
#include "include/memory.h"
#include "include/dram-address.h"
#include "include/hammer-suite.h"
#include "include/params.h"

ProfileParams *p;

DRAMLayout      g_mem_layout = {{{0x4080,0x48000,0x90000,0x120000,0x1b300}, 5}, 0xffffc0000, ROW_SIZE-1};

void read_config(SessionConfig * cfg, char *f_name)
{
	FILE *fp = fopen(f_name, "rb");
	int p_size;
	size_t res;
	assert(fp != NULL);
	res = fread(cfg, sizeof(SessionConfig), 1, fp);
	assert(res == 1);
	fclose(fp);
	return;
}

void gmem_dump()
{
	FILE *fp = fopen("g_mem_dump.bin", "wb+");
	fwrite(&g_mem_layout, sizeof(DRAMLayout), 1, fp);
	fclose(fp);

#ifdef DEBUG
	DRAMLayout tmp;
	fp = fopen("g_mem_dump.bin", "rb");
	fread(&tmp, sizeof(DRAMLayout), 1, fp);
	fclose(fp);

	assert(tmp->h_fns->len == g_mem_layout->h_fns->len);
	assert(tmp->bank == g_mem_layout->bank);
	assert(tmp->row == g_mem_layout->row);
	assert(tmp->col == g_mem_layout->col);

#endif
}

int main(int argc, char **argv)
{
	srand(time(NULL));
	p = (ProfileParams*)malloc(sizeof(ProfileParams));
	if (p == NULL) {
		fprintf(stderr, "[ERROR] Memory allocation\n");
		exit(1);
	}

	if(process_argv(argc, argv, p) == -1) {
		free(p);
		exit(1);
	}

	MemoryBuffer mem = {
		.buffer = NULL,
		.physmap = NULL,
		.fd = p->huge_fd,
		.size = p->m_size,
		.align = p->m_align,
		.flags = p->g_flags & MEM_MASK
	};

	alloc_buffer(&mem);
	set_physmap(&mem);
	gmem_dump();

	SessionConfig s_cfg;
	memset(&s_cfg, 0, sizeof(SessionConfig));
	if (p->g_flags & F_CONFIG) {
		read_config(&s_cfg, p->conf_file);
	} else {
		// HARDCODED values
		s_cfg.h_rows = PATT_LEN;
		s_cfg.h_rounds = p->rounds;
		s_cfg.h_cfg = N_SIDED;
		s_cfg.d_cfg = RANDOM;
		s_cfg.base_off = p->base_off;
		s_cfg.aggr_n = p->aggr;
	}

	if (p->fuzzing) {
		fuzzing_session(&s_cfg, &mem);
	} else {
		hammer_session(&s_cfg, &mem);
	}

	close(p->huge_fd);
	return 0;
}

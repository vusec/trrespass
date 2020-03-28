#include "include/hammer-suite.h"

#include "include/memory.h"
#include "include/utils.h"
#include "include/allocator.h"
#include "include/dram-address.h"
#include "include/addr-mapper.h"
#include "include/params.h"

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
#include <sched.h>
#include <limits.h>
#include <math.h>

#define REFRESH_VAL "stdrefi"
#define OUT_HEAD "f_og, f_new, vict_addr, aggr_addr\n"

#define ROW_FIELD 		1
#define COL_FIELD 		1<<1
#define BK_FIELD 		1<<2
#define P_FIELD			1<<3
#define ALL_FIELDS		(ROW_FIELD | COL_FIELD | BK_FIELD)
#define FLIPTABLE

/*
 h_patt		= hammer pattern (e.g., DOUBLE_SIDED)
 d_patt		= data pattern	(e.g., RANDOM)
 vict_addr	= DRAMAddr for the bit flip in format bkXX.XrXXXX.cXXX
 f_og 		= byte original value
 f_new		= byte after bit flip
 f_mask		= bitmask of the bit flip
 base_row	= initial row being hammered *in this round*
 row_cnt	= number of rows being hammered *in this round*
 t_refi		= t_refi
 h_rounds	= number of hammering rounds
 aggr_addr	= addresses of aggressor rows in format bkXX.rXXXX.cXX

 */

#define SHADOW_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE)
#define DEBUG

#define NOP asm volatile ("NOP":::);
#define NOP10 NOP NOP NOP NOP NOP NOP NOP NOP NOP NOP
#define NOP100 NOP10 NOP10 NOP10 NOP10 NOP10 NOP10 NOP10 NOP10 NOP10 NOP10
#define NOP1000 NOP100 NOP100 NOP100 NOP100 NOP100 NOP100 NOP100 NOP100 NOP100 NOP100

extern ProfileParams *p;

int g_bk;
FILE *out_fd            = NULL;
static uint64_t CL_SEED = 0x7bc661612e71168c;

static inline __attribute((always_inline))
char *cl_rand_gen(DRAMAddr * d_addr)
{
	static uint64_t cl_buff[8];
	for (int i = 0; i < 8; i++) {
		cl_buff[i] =
		    __builtin_ia32_crc32di(CL_SEED,
					   (d_addr->row + d_addr->bank +
					    (d_addr->col + i*8)));
	}
	return (char *)cl_buff;
}

typedef struct {
	DRAMAddr *d_lst;
	size_t len;
	size_t rounds;
} HammerPattern;

typedef struct {
	DRAMAddr d_vict;
	uint8_t f_og;
	uint8_t f_new;
	HammerPattern *h_patt;
} FlipVal;

typedef struct {
	MemoryBuffer *mem;
	SessionConfig *cfg;
	DRAMAddr d_base;	// base address for hammering
	ADDRMapper *mapper;	// dram mapper

	int (*hammer_test) (void *self);
} HammerSuite;

char *dAddr_2_str(DRAMAddr d_addr, uint8_t fields)
{
	static char ret_str[64];
	char tmp_str[10];
	bool first = true;
	memset(ret_str, 0x00, 64);
	if (fields & ROW_FIELD) {
		first = false;
		sprintf(tmp_str, "r%05ld", d_addr.row);
		strcat(ret_str, tmp_str);
	}
	if (fields & BK_FIELD) {
		if (!first) {
			strcat(ret_str, ".");
		}
		sprintf(tmp_str, "bk%02ld", d_addr.bank);
		strcat(ret_str, tmp_str);
		first = false;
	}
	if (fields & COL_FIELD) {
		if (!first) {
			strcat(ret_str, ".");
		}
		sprintf(tmp_str, "col%04ld", d_addr.col);
		strcat(ret_str, tmp_str);
		first = false;
	}
	return ret_str;
}

char *hPatt_2_str(HammerPattern * h_patt, int fields)
{
	static char patt_str[256];
	char *dAddr_str;

	memset(patt_str, 0x00, 256);

	for (int i = 0; i < h_patt->len; i++) {
		dAddr_str = dAddr_2_str(h_patt->d_lst[i], fields);
		strcat(patt_str, dAddr_str);
		if (i + 1 != h_patt->len) {
			strcat(patt_str, "/");
		}

	}
	return patt_str;
}

void print_start_attack(HammerPattern *h_patt)
{
	fprintf(out_fd, "%s : ", hPatt_2_str(h_patt, ROW_FIELD | BK_FIELD));
	fflush(out_fd);
}

void print_end_attack()
{
	fprintf(out_fd, "\n");
	fflush(out_fd);
}

void export_flip(FlipVal * flip)
{
	if (p->g_flags & F_VERBOSE) {
		fprintf(stdout, "[FLIP] - (%02x => %02x)\t vict: %s \taggr: %s \n",
				flip->f_og, flip->f_new, dAddr_2_str(flip->d_vict, ALL_FIELDS),
				hPatt_2_str(flip->h_patt, ROW_FIELD | BK_FIELD));
		fflush(stdout);
	}

#ifdef FLIPTABLE
		fprintf(out_fd, "%02x,%02x,%s ", flip->f_og, flip->f_new,
				dAddr_2_str(flip->d_vict, ALL_FIELDS));
#else
		fprintf(out_fd, "%02x,%02x,%s,%s\n", flip->f_og, flip->f_new,
				dAddr_2_str(flip->d_vict, ALL_FIELDS), hPatt_2_str(flip->h_patt,
					ROW_FIELD | BK_FIELD | P_FIELD));
#endif
	fflush(out_fd);
}

void export_cfg(HammerSuite * suite)
{
	SessionConfig *cfg = suite->cfg;

	if (p->g_flags & F_VERBOSE) {
		fprintf(stdout,
			"Config: { h_cfg: %s, d_cfg: %s, h_rows: %ld, h_rounds: %ld, base: %s}\n",
			config_str[cfg->h_cfg], data_str[cfg->d_cfg],
			cfg->h_rows, cfg->h_rounds, dAddr_2_str(suite->d_base,
								ROW_FIELD |
								BK_FIELD));
	}

	fprintf(out_fd,
		"# { h_cfg: %s, d_cfg: %s, h_rows: %ld, h_rounds: %ld, base: %s}\n",
		config_str[cfg->h_cfg], data_str[cfg->d_cfg], cfg->h_rows,
		cfg->h_rounds, dAddr_2_str(suite->d_base,
					   ROW_FIELD | BK_FIELD));
	fflush(out_fd);
}

void swap(char **lst, int i, int j)
{
	char *tmp = lst[i];
	lst[i] = lst[j];
	lst[j] = tmp;
}

int random_int(int min, int max)
{
	int number = min + rand() % (max - min);
	return number;
}

uint64_t hammer_it(HammerPattern* patt, MemoryBuffer* mem) {

	char** v_lst = (char**) malloc(sizeof(char*)*patt->len);
	for (size_t i = 0; i < patt->len; i++) {
		v_lst[i] = phys_2_virt(dram_2_phys(patt->d_lst[i]), mem);
	}

	sched_yield();
	if (p->threshold > 0) {
		uint64_t t0 = 0, t1 = 0;
		// Threshold value depends on your system
		while (abs((int64_t) t1 - (int64_t) t0) < p->threshold) {
			t0 = rdtscp();
			*(volatile char *)v_lst[0];
			clflushopt(v_lst[0]);
			t1 = rdtscp();
		}
	}


	uint64_t cl0, cl1;
	cl0 = realtime_now();
	for ( int i = 0; i < patt->rounds;  i++) {
		mfence();
		for (size_t j = 0; j < patt->len; j++) {
			*(volatile char*) v_lst[j];
		}
		for (size_t j = 0; j < patt->len; j++) {
			clflushopt(v_lst[j]);
		}
	}
	cl1 = realtime_now();

	free(v_lst);
	return (cl1-cl0) / 1000000;

}

void __test_fill_random(char *addr, size_t size)
{
	int fd;
	if ((fd = open("/dev/urandom", O_RDONLY)) == -1) {
		perror("[ERROR] - Unable to open /dev/urandom");
		exit(1);
	}
	if (read(fd, addr, size) == -1) {
		perror("[ERROR] - Unable to read /dev/urandom");
		exit(1);
	}
	close(fd);

}

// DRAMAddr needs to be a copy in order to leave intact the original address
void fill_stripe(DRAMAddr d_addr, uint8_t val, ADDRMapper * mapper)
{
	for (size_t col = 0; col < ROW_SIZE; col += (1 << 6)) {
		d_addr.col = col;
		DRAM_pte d_pte = get_dram_pte(mapper, &d_addr);
		memset(d_pte.v_addr, val, CL_SIZE);
	}

}

void fill_row(HammerSuite *suite, DRAMAddr *d_addr, HammerData data_patt, int reverse)
{
	if (p->vpat != (void *)NULL && p->tpat != (void *)NULL) {
		uint8_t pat = reverse ? *p->vpat : *p->tpat;
		fill_stripe(*d_addr, pat, suite->mapper);
		return;
	}

	if (reverse) {
		data_patt = (HammerData)((int)data_patt ^(int)REVERSE);
	}

	switch (data_patt) {
	case RANDOM:
		// rows are already filled for random data patt
		break;
	case ONE_TO_ZERO:
		fill_stripe(*d_addr, 0x00, suite->mapper);
		break;
	case ZERO_TO_ONE:
		fill_stripe(*d_addr, 0xff, suite->mapper);
		break;
	default:
		// fprintf(stderr, "[ERROR] - Wrong data pattern %d\n", data_patt);
		// exit(1);
		break;
	}

}

void cl_rand_fill(DRAM_pte * pte)
{
	char *rand_data = cl_rand_gen(&pte->d_addr);
	memcpy(pte->v_addr, rand_data, CL_SIZE);
}

uint64_t cl_rand_comp(DRAM_pte * pte)
{
	char *rand_data = cl_rand_gen(&pte->d_addr);
	uint64_t res = 0;
	for (int i = 0; i < CL_SIZE; i++) {
		if (*(pte->v_addr + i) != rand_data[i]) {
			res |= 1UL<<i;
		}
	}
	return res;
}

void init_random(HammerSuite * suite)
{
	int fd;
	if ((fd = open("/dev/urandom", O_RDONLY)) == -1) {
		perror("[ERROR] - Unable to open /dev/urandom");
		exit(1);
	}
	if (CL_SEED == 0) {
		if (read(fd, &CL_SEED, sizeof(CL_SEED)) == -1) {
			perror("[ERROR] - Unable to read /dev/urandom");
			exit(1);
		}
	}
	// fprintf(out_fd,"#seed: %lx\n", CL_SEED);
	close(fd);
	ADDRMapper *mapper = suite->mapper;
	DRAMAddr d_tmp;
	for (size_t bk = 0; bk < get_banks_cnt(); bk++) {
		d_tmp.bank = bk;
		for (size_t row = 0; row < suite->cfg->h_rows; row++) {
			d_tmp.row = suite->mapper->base_row + row;
			for (size_t col = 0; col < ROW_SIZE; col += (1 << 6)) {
				d_tmp.col = col;
				DRAM_pte d_pte = get_dram_pte(mapper, &d_tmp);
				cl_rand_fill(&d_pte);
			}
		}
	}
}

void init_stripe(HammerSuite * suite, uint8_t val)
{
	ADDRMapper *mapper = suite->mapper;
	DRAMAddr d_tmp;
	for (size_t bk = 0; bk < get_banks_cnt(); bk++) {
		d_tmp.bank = bk;
		for (size_t row = 0; row < suite->cfg->h_rows; row++) {
			d_tmp.row = suite->mapper->base_row + row;
			for (size_t col = 0; col < ROW_SIZE; col += (1 << 6)) {
				d_tmp.col = col;
				DRAM_pte d_pte = get_dram_pte(mapper, &d_tmp);
				memset(d_pte.v_addr, val, CL_SIZE);
			}
		}
	}
}

void init_chunk(HammerSuite * suite)
{

	if (p->vpat != (void *)NULL && p->tpat != (void *)NULL) {
		init_stripe(suite, (uint8_t) * p->vpat);
		return;
	}

	SessionConfig *cfg = suite->cfg;
	switch (cfg->d_cfg) {
	case RANDOM:
		init_random(suite);
		break;
	case ONE_TO_ZERO:
		init_stripe(suite, 0xff);
		break;
	case ZERO_TO_ONE:
		init_stripe(suite, 0x00);
		break;
	default:
		fprintf(stderr, "[ERROR] - Wrong data pattern %d\n",
			cfg->d_cfg);
		exit(1);
		break;
	}
}

void scan_random(HammerSuite * suite, HammerPattern * h_patt, size_t adj_rows)
{
	ADDRMapper *mapper = suite->mapper;
	SessionConfig *cfg = suite->cfg;

	DRAMAddr d_tmp;
	FlipVal flip;

	d_tmp.bank = h_patt->d_lst[0].bank;

	for (size_t row = 0; row < cfg->h_rows; row++) {
		d_tmp.row = suite->mapper->base_row + row;
		for (size_t col = 0; col < ROW_SIZE; col += (1 << 6)) {
			d_tmp.col = col;
			DRAM_pte pte = get_dram_pte(mapper, &d_tmp);
			clflush(pte.v_addr);
			cpuid();
			uint64_t res = cl_rand_comp(&pte);
			if (res) {
				char *rand_data = cl_rand_gen(&pte.d_addr);
				for (int off = 0; off < CL_SIZE; off++) {
					if (!((res >> off) & 1))
						continue;
					d_tmp.col += off;

					flip.d_vict = d_tmp;
					flip.f_og = (uint8_t) rand_data[off];
					flip.f_new = *(uint8_t *) (pte.v_addr + off);
					flip.h_patt = h_patt;
					assert(flip.f_og != flip.f_new);
					export_flip(&flip);

				}
				memcpy((char *)(pte.v_addr), rand_data, CL_SIZE);
			}
		}
	}
}

int find_flip(HammerSuite * suite, HammerPattern * h_patt, FlipVal *orig)
{
	ADDRMapper *mapper = suite->mapper;
	SessionConfig *cfg = suite->cfg;

	DRAMAddr d_tmp;
	FlipVal flip;;

	d_tmp.bank = orig->d_vict.bank;
	d_tmp.row = orig->d_vict.row;
	d_tmp.col = orig->d_vict.col;

	DRAM_pte pte = get_dram_pte(mapper, &d_tmp);
	clflush(pte.v_addr);
	cpuid();
	int off = cl_rand_comp(&pte);
	if (off != -1) {
		return 1;
	}

	return 0;
}

bool in_hPatt(DRAMAddr * d_addr, HammerPattern * h_patt)
{
	for (int i = 0; i < h_patt->len; i++) {
		if (d_addr_eq_row(&h_patt->d_lst[i], d_addr))
			return true;
	}
	return false;
}

uint64_t cl_stripe_cmp(DRAM_pte * pte, uint8_t val)
{
	uint64_t res = 0;
#ifdef POINTER_CHAISING
	for (int_t i = 8; i < CL_SIZE; i++) {
#else
	for (int i = 0; i < CL_SIZE; i++) {
#endif
		if (*(uint8_t*) (pte->v_addr + i) != val) {
			res |= 1UL<<i;
		}
	}
	return res;
}

void scan_stripe(HammerSuite * suite, HammerPattern * h_patt, size_t adj_rows,
		 uint8_t val)
{
	ADDRMapper *mapper = suite->mapper;
	SessionConfig *cfg = suite->cfg;

	DRAMAddr d_tmp;
	FlipVal flip;

	d_tmp.bank = h_patt->d_lst[0].bank;
	uint8_t t_val = val;

	for (size_t row = 0; row < cfg->h_rows; row++) {
		d_tmp.row = suite->mapper->base_row + row;
		t_val = val;
		if (in_hPatt(&d_tmp, h_patt))
			if (p->tpat != (void *)NULL && p->vpat != (void *)NULL)
				t_val = (uint8_t) * p->tpat;
			else
				t_val ^= 0xff;

		for (size_t col = 0; col < ROW_SIZE; col += (1 << 6)) {
			d_tmp.col = col;
			DRAM_pte pte = get_dram_pte(mapper, &d_tmp);
			clflush(pte.v_addr);
			cpuid();

			uint64_t res = cl_stripe_cmp(&pte, t_val);
			if (res) {
				for (int off = 0; off < CL_SIZE; off++) {
					if (!((res >> off) & 1))
						continue;
					d_tmp.col += off;

					flip.d_vict = d_tmp;
					flip.f_og = (uint8_t) t_val;
					flip.f_new = *(uint8_t *) (pte.v_addr + off);
					flip.h_patt = h_patt;
					export_flip(&flip);
					memset(pte.v_addr + off, t_val, 1);
				}
				memset((char *)(pte.v_addr), t_val, CL_SIZE);
			}
		}
	}
}

// TODO adj_rows should tell how many rows to scan out of the bank. Not currently used
void scan_rows(HammerSuite * suite, HammerPattern * h_patt, size_t adj_rows)
{
	if (p->vpat != (void *)NULL && p->tpat != (void *)NULL) {
		scan_stripe(suite, h_patt, adj_rows, (uint8_t) * p->vpat);
		return;
	}

	SessionConfig *cfg = suite->cfg;
	switch (cfg->d_cfg) {
	case RANDOM:
		// rows are already filled for random data patt
		scan_random(suite, h_patt, adj_rows);
		break;
	case ONE_TO_ZERO:
		scan_stripe(suite, h_patt, adj_rows, 0xff);
		break;
	case ZERO_TO_ONE:
		scan_stripe(suite, h_patt, adj_rows, 0x00);
		break;
	default:
		fprintf(stderr, "[ERROR] - Wrong data pattern %d\n",
			cfg->d_cfg);
		exit(1);
		break;
	}
}

int free_triple_sided_test(HammerSuite * suite)
{
	MemoryBuffer *mem = suite->mem;
	SessionConfig *cfg = suite->cfg;

	DRAMAddr d_base = suite->d_base;
	d_base.col = 0;
	HammerPattern h_patt;

	h_patt.len = 3;
	h_patt.rounds = cfg->h_rounds;

	h_patt.d_lst = (DRAMAddr *) malloc(sizeof(DRAMAddr) * h_patt.len);
	memset(h_patt.d_lst, 0x00, sizeof(DRAMAddr) * h_patt.len);

	init_chunk(suite);
	fprintf(stderr, "CL_SEED: %lx\n", CL_SEED);

	h_patt.d_lst[0] = d_base;
	for (int r0 = 1; r0 < cfg->h_rows; r0++) {
		for (int r1 = r0; r1 < cfg->h_rows; r1++) {
			if (r0 == r1)
				continue;

			h_patt.d_lst[1].row = h_patt.d_lst[0].row + r0;
			h_patt.d_lst[2].row = h_patt.d_lst[0].row + r1;
			h_patt.d_lst[0].bank = 0;
			h_patt.d_lst[1].bank = 0;
			h_patt.d_lst[2].bank = 0;
			fprintf(stderr, "[HAMMER] - %s: ", hPatt_2_str(&h_patt, ROW_FIELD));
			for (size_t bk = 0; bk < get_banks_cnt(); bk++) {
				h_patt.d_lst[0].bank = bk;
				h_patt.d_lst[1].bank = bk;
				h_patt.d_lst[2].bank = bk;
				// fill all the aggressor rows
				for (int idx = 0; idx < 3; idx++) {
					fill_row(suite, &h_patt.d_lst[idx], cfg->d_cfg, 0);
				}
				uint64_t time = hammer_it(&h_patt, mem);
				fprintf(stderr, "%ld ", time);

				scan_rows(suite, &h_patt, 0);
				for (int idx = 0; idx < 3; idx++) {
					fill_row(suite, &h_patt.d_lst[idx], cfg->d_cfg, 1);
				}
			}
			fprintf(stderr, "\n");
		}
	}
	free(h_patt.d_lst);
}

int assisted_double_sided_test(HammerSuite * suite)
{
	MemoryBuffer *mem = suite->mem;
	SessionConfig *cfg = suite->cfg;
	DRAMAddr d_base = suite->d_base;
	d_base.col = 0;

	HammerPattern h_patt;

	h_patt.len = 3;
	h_patt.rounds = cfg->h_rounds;

	h_patt.d_lst = (DRAMAddr *) malloc(sizeof(DRAMAddr) * h_patt.len);
	memset(h_patt.d_lst, 0x00, sizeof(DRAMAddr) * h_patt.len);

	init_chunk(suite);
	fprintf(stderr, "CL_SEED: %lx\n", CL_SEED);
	h_patt.d_lst[0] = d_base;

	for (int r0 = 1; r0 < cfg->h_rows; r0++) {
		h_patt.d_lst[1].row = d_base.row + r0;
		h_patt.d_lst[2].row = h_patt.d_lst[1].row + 2;
		h_patt.d_lst[0].row =
		    d_base.row + get_rnd_int(0, cfg->h_rows - 1);
		while (h_patt.d_lst[0].row == h_patt.d_lst[1].row
		       || h_patt.d_lst[0].row == h_patt.d_lst[2].row)
			h_patt.d_lst[0].row =
			    d_base.row + get_rnd_int(0, cfg->h_rows - 1);

		if (h_patt.d_lst[2].row >= d_base.row + cfg->h_rows)
			break;

		h_patt.d_lst[0].bank = 0;
		h_patt.d_lst[1].bank = 0;
		h_patt.d_lst[2].bank = 0;
		fprintf(stderr, "[HAMMER] - %s: ", hPatt_2_str(&h_patt, ROW_FIELD));
		for (size_t bk = 0; bk < get_banks_cnt(); bk++) {
			h_patt.d_lst[0].bank = bk;
			h_patt.d_lst[1].bank = bk;
			h_patt.d_lst[2].bank = bk;
			// fill all the aggressor rows
			for (int idx = 0; idx < 3; idx++) {
				fill_row(suite, &h_patt.d_lst[idx], cfg->d_cfg, 0);
				// fprintf(stderr, "d_addr: %s\n", dram_2_str(&h_patt.d_lst[idx]));
			}
			// fprintf(stderr, "d_addr: %s\n", dram_2_str(&h_patt.d_lst[idx]));
			uint64_t time = hammer_it(&h_patt, mem);
			fprintf(stderr, "%ld ", time);

			scan_rows(suite, &h_patt, 0);
			for (int idx = 0; idx<3; idx++) {
				fill_row(suite, &h_patt.d_lst[idx], cfg->d_cfg, 1);
			}
		}
		fprintf(stderr, "\n");
	}
	free(h_patt.d_lst);
}

int n_sided_test(HammerSuite * suite)
{
	MemoryBuffer *mem = suite->mem;
	SessionConfig *cfg = suite->cfg;
	DRAMAddr d_base = suite->d_base;
	d_base.col = 0;
	/* d_base.row = 20480; */
    /* d_base.row = 16400; */
	HammerPattern h_patt;

	h_patt.len = cfg->aggr_n;
	h_patt.rounds = cfg->h_rounds;

	h_patt.d_lst = (DRAMAddr *) malloc(sizeof(DRAMAddr) * h_patt.len);
	memset(h_patt.d_lst, 0x00, sizeof(DRAMAddr) * h_patt.len);

	init_chunk(suite);
	fprintf(stderr, "CL_SEED: %lx\n", CL_SEED);
	h_patt.d_lst[0] = d_base;

	const int mem_to_hammer = 256 << 20;
	const int n_rows = mem_to_hammer / ((8<<10) *  get_banks_cnt());
	fprintf(stderr, "Hammering %d rows per bank\n", n_rows);
	for (int r0 = 1; r0 < n_rows; r0++) {
		h_patt.d_lst[0].row = d_base.row + r0;
		int k = 1;
		for (; k < cfg->aggr_n; k++) {
			h_patt.d_lst[k].row = h_patt.d_lst[k - 1].row + 2;
			h_patt.d_lst[k].bank = 0;
		}
		if (h_patt.d_lst[k - 1].row >= d_base.row + cfg->h_rows)
			break;

		fprintf(stderr, "[HAMMER] - %s: ", hPatt_2_str(&h_patt, ROW_FIELD));
		for (size_t bk = 0; bk < get_banks_cnt(); bk++) {

			for (int s = 0; s < cfg->aggr_n; s++) {
				h_patt.d_lst[s].bank = bk;
			}
#ifdef FLIPTABLE
				print_start_attack(&h_patt);
#endif
			// fill all the aggressor rows
			for (int idx = 0; idx < cfg->aggr_n; idx++) {
				fill_row(suite, &h_patt.d_lst[idx], cfg->d_cfg, 0);
			}

			uint64_t time = hammer_it(&h_patt, mem);
			fprintf(stderr, "%ld ", time);

			scan_rows(suite, &h_patt, 0);
			for (int idx = 0; idx<h_patt.len; idx++) {
				fill_row(suite, &h_patt.d_lst[idx], cfg->d_cfg, 1);
			}
#ifdef FLIPTABLE
				print_end_attack();
#endif
		}
		fprintf(stderr, "\n");
	}
	free(h_patt.d_lst);
}

void fuzz(HammerSuite *suite, int d, int v)
{
	int i;
	HammerPattern h_patt;
	SessionConfig *cfg = suite->cfg;
	h_patt.rounds = cfg->h_rounds;
	h_patt.len = cfg->aggr_n;

	h_patt.d_lst = (DRAMAddr *) malloc(sizeof(DRAMAddr) * h_patt.len);
	memset(h_patt.d_lst, 0x00, sizeof(DRAMAddr) * h_patt.len);

	init_chunk(suite);
	int offset = random_int(1, 32);

	h_patt.d_lst[0] = suite->d_base;
	h_patt.d_lst[0].row = suite->d_base.row + offset;

	h_patt.d_lst[1] = suite->d_base;
	h_patt.d_lst[1].row = h_patt.d_lst[0].row + v + 1;
	for (i = 2; i < h_patt.len-1; i+=2) {
		h_patt.d_lst[i] = suite->d_base;
		h_patt.d_lst[i].row = h_patt.d_lst[i-1].row + d + 1;
		h_patt.d_lst[i+1] = suite->d_base;
		h_patt.d_lst[i+1].row = h_patt.d_lst[i].row + v + 1;
	}
	if (h_patt.len % 2) {
		h_patt.d_lst[h_patt.len-1] = suite->d_base;
		h_patt.d_lst[h_patt.len-1].row = h_patt.d_lst[h_patt.len-2].row + d + 1;
	}

	fprintf(stderr, "[HAMMER] - %s: ", hPatt_2_str(&h_patt, ROW_FIELD));
	for (int bk = 0; bk < get_banks_cnt(); bk++)
	{
		for (int idx = 0; idx < h_patt.len; idx++) {
			h_patt.d_lst[idx].bank = bk;
		}
#ifdef FLIPTABLE
		print_start_attack(&h_patt);
#endif
		for (int idx = 0; idx < h_patt.len; idx++)
			fill_row(suite, &h_patt.d_lst[idx], suite->cfg->d_cfg, 0);

		uint64_t time = hammer_it(&h_patt, suite->mem);
		fprintf(stderr, "%lu ",time);

		scan_rows(suite, &h_patt, 0);
		for (int idx = 0; idx<h_patt.len; idx++) {
			fill_row(suite, &h_patt.d_lst[idx], suite->cfg->d_cfg, 1);
		}

#ifdef FLIPTABLE
		print_end_attack();
#endif
	}
	fprintf(stdout, "\n");
	free(h_patt.d_lst);
}

void create_dir(const char* dir_name)
{
	struct stat st = {0};
	if (stat(dir_name, &st) == -1) {
			mkdir(dir_name, 0777);
	}
}

void fuzzing_session(SessionConfig * cfg, MemoryBuffer * mem)
{
	int d, v, aggrs;

	srand(CL_SEED);
	DRAMAddr d_base = phys_2_dram(virt_2_phys(mem->buffer, mem));
	fprintf(stdout, "[INFO] d_base.row:%lu\n", d_base.row);

	/* Init FILES */
	create_dir(DATA_DIR);
	char *out_name = (char *)malloc(500);
	char rows_str[10];
	strcpy(out_name, DATA_DIR);
	strcat(out_name, p->g_out_prefix);
	strcat(out_name, ".");
	strcat(out_name, "fuzzing");
	strcat(out_name, ".");
	sprintf(rows_str, "%08ld", d_base.row);
	strcat(out_name, rows_str);
	strcat(out_name, ".");
	sprintf(rows_str, "%ld", cfg->h_rounds);
	strcat(out_name, rows_str);
	strcat(out_name, ".");
	strcat(out_name, REFRESH_VAL);
	strcat(out_name, ".csv");
	if (p->g_flags & F_NO_OVERWRITE) {
		int cnt = 0;
		char *tmp_name = (char *)malloc(500);
		strncpy(tmp_name, out_name, strlen(out_name));
		while (access(tmp_name, F_OK) != -1) {
			cnt++;
			sprintf(tmp_name, "%s.%02d", out_name, cnt);
		}
		strncpy(out_name, tmp_name, strlen(tmp_name));
		free(tmp_name);
	}
	out_fd = fopen(out_name, "w+");
	assert(out_fd != NULL);

	HammerSuite *suite = (HammerSuite *) malloc(sizeof(HammerSuite));
	suite->mem = mem;
	suite->cfg = cfg;
	suite->d_base = d_base;
	suite->mapper = (ADDRMapper *) malloc(sizeof(ADDRMapper));
	init_addr_mapper(suite->mapper, mem, &suite->d_base, cfg->h_rows);

	while(1) {
		cfg->aggr_n = random_int(2, 32);
		d = random_int(0, 16);
		v = random_int(1, 4);
		fuzz(suite, d, v);
	}
}

void hammer_session(SessionConfig * cfg, MemoryBuffer * memory)
{
	MemoryBuffer mem = *memory;

	DRAMAddr d_base = phys_2_dram(virt_2_phys(mem.buffer, &mem));
	d_base.row += cfg->base_off;

	create_dir(DATA_DIR);
	char *out_name = (char *)malloc(500);
	char rows_str[10];
	strcpy(out_name, DATA_DIR);
	strcat(out_name, p->g_out_prefix);
	strcat(out_name, ".");

	if (cfg->h_cfg) {
		char config[15];
		sprintf(config, config_str[cfg->h_cfg], cfg->aggr_n);
		strcat(out_name, config);
	} else {
		strcat(out_name, config_str[cfg->h_cfg]);
	}
	strcat(out_name, ".");
	sprintf(rows_str, "%08ld", d_base.row);
	strcat(out_name, rows_str);
	strcat(out_name, ".");
	sprintf(rows_str, "%03ld", cfg->h_rows);
	strcat(out_name, rows_str);
	strcat(out_name, ".");
	sprintf(rows_str, "%ld", cfg->h_rounds);
	strcat(out_name, rows_str);
	strcat(out_name, ".");
	strcat(out_name, REFRESH_VAL);
	strcat(out_name, ".csv");
	if (p->g_flags & F_NO_OVERWRITE) {
		int cnt = 0;
		char *tmp_name = (char *)malloc(500);
		strncpy(tmp_name, out_name, strlen(out_name));
		while (access(tmp_name, F_OK) != -1) {
			cnt++;
			sprintf(tmp_name, "%s.%02d", out_name, cnt);
		}
		strncpy(out_name, tmp_name, strlen(tmp_name));
		free(tmp_name);
	}
	out_fd = fopen(out_name, "w+");

	fprintf(stderr,
		"[LOG] - Hammer session! access pattern: %s\t data pattern: %s\n",
		config_str[cfg->h_cfg], data_str[cfg->d_cfg]);
	fprintf(stderr, "[LOG] - File: %s\n", out_name);

	HammerSuite *suite = (HammerSuite *) malloc(sizeof(HammerSuite));
	suite->cfg = cfg;
	suite->mem = &mem;
	suite->d_base = d_base;
	suite->mapper = (ADDRMapper *) malloc(sizeof(ADDRMapper));
	init_addr_mapper(suite->mapper, &mem, &suite->d_base, cfg->h_rows);

#ifndef FLIPTABLE
	export_cfg(suite);	// export the configuration of the experiment to file.
	fprintf(out_fd, OUT_HEAD);
#endif

	switch (cfg->h_cfg) {
		case ASSISTED_DOUBLE_SIDED:
		{
			suite->hammer_test =
			    (int (*)(void *))assisted_double_sided_test;
			break;
		}
		case FREE_TRIPLE_SIDED:
		{
			suite->hammer_test =
			    (int (*)(void *))free_triple_sided_test;
			break;
		}
		case N_SIDED:
		{
			assert(cfg->aggr_n > 1);
			suite->hammer_test = (int (*)(void *))n_sided_test;
			break;
		}
		default:
		{
			suite->hammer_test = (int (*)(void *))n_sided_test;
		}
	}

	suite->hammer_test(suite);
	fclose(out_fd);
	tear_down_addr_mapper(suite->mapper);
	free(suite);
}

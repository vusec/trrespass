#include "dram-address.h"

#include <stdio.h>
#include <stdlib.h>

#include "utils.h"

#define DEBUG_REVERSE_FN 1

extern DRAMLayout g_mem_layout;

uint64_t get_dram_row(physaddr_t p_addr)
{
	return (p_addr & g_mem_layout.
		row_mask) >> __builtin_ctzl(g_mem_layout.row_mask);
}

uint64_t get_dram_col(physaddr_t p_addr)
{
	return (p_addr & g_mem_layout.
		col_mask) >> __builtin_ctzl(g_mem_layout.col_mask);
}

DRAMAddr phys_2_dram(physaddr_t p_addr)
{

	DRAMAddr res = { 0, 0, 0 };
	for (int i = 0; i < g_mem_layout.h_fns.len; i++) {
		res.bank |=
		    (__builtin_parityl(p_addr & g_mem_layout.h_fns.lst[i]) <<
		     i);
	}

	res.row = get_dram_row(p_addr);
	res.col = get_dram_col(p_addr);

	return res;
}

physaddr_t dram_2_phys(DRAMAddr d_addr)
{
	physaddr_t p_addr = 0;
	uint64_t col_val = 0;

	p_addr = (d_addr.row << __builtin_ctzl(g_mem_layout.row_mask));	// set row bits
	p_addr |= (d_addr.col << __builtin_ctzl(g_mem_layout.col_mask));	// set col bits

	for (int i = 0; i < g_mem_layout.h_fns.len; i++) {
		uint64_t masked_addr = p_addr & g_mem_layout.h_fns.lst[i];
		// if the address already respects the h_fn then just move to the next func
		if (__builtin_parity(masked_addr) == ((d_addr.bank >> i) & 1L)) {
			continue;
		}
		// else flip a bit of the address so that the address respects the dram h_fn
		// that is get only bits not affecting the row.
		uint64_t h_lsb = __builtin_ctzl((g_mem_layout.h_fns.lst[i]) &
						~(g_mem_layout.col_mask) &
						~(g_mem_layout.row_mask));
		p_addr ^= 1 << h_lsb;
	}

#if DEBUG_REVERSE_FN
	int correct = 1;
	for (int i = 0; i < g_mem_layout.h_fns.len; i++) {

		if (__builtin_parity(p_addr & g_mem_layout.h_fns.lst[i]) !=
		    ((d_addr.bank >> i) & 1L)) {
			correct = 0;
			break;
		}
	}
	if (d_addr.row != ((p_addr &
			    g_mem_layout.row_mask) >>
			   __builtin_ctzl(g_mem_layout.row_mask)))
		correct = 0;
	if (!correct)
		fprintf(stderr,
			"[DEBUG] - Mapping function for 0x%lx not respected\n",
			p_addr);

#endif

	return p_addr;
}

void set_global_dram_layout(DRAMLayout & mem_layout)
{
	g_mem_layout = mem_layout;
}

DRAMLayout *get_dram_layout()
{
	return &g_mem_layout;
}

bool d_addr_eq(DRAMAddr * d1, DRAMAddr * d2)
{
	return (d1->bank == d2->bank) && (d1->row == d2->row)
	    && (d1->col == d2->col);
}

bool d_addr_eq_row(DRAMAddr * d1, DRAMAddr * d2)
{
	return (d1->bank == d2->bank) && (d1->row == d2->row);
}

uint64_t get_banks_cnt()
{
	return 1 << g_mem_layout.h_fns.len;

}

char *dram_2_str(DRAMAddr * d_addr)
{
	static char ret_str[1024];
	sprintf(ret_str, "DRAM(bk: %ld (%s), row: %08ld, col: %08ld)",
		d_addr->bank, int_2_bin(d_addr->bank), d_addr->row,
		d_addr->col);
	return ret_str;
}

char *dramLayout_2_str(DRAMLayout * mem_layout)
{
	static char ret_str[1024];
	sprintf(ret_str, "{0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx} - 0x%lx\n",
		mem_layout->h_fns.lst[0], mem_layout->h_fns.lst[1],
		mem_layout->h_fns.lst[2], mem_layout->h_fns.lst[3],
		mem_layout->h_fns.lst[4], mem_layout->h_fns.lst[5],
		mem_layout->row_mask);
	return ret_str;
}

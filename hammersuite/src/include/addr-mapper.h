#pragma once

#include "memory.h"
#include "types.h"
#include "dram-address.h"

typedef struct {
	DRAMAddr d_addr;
	char *v_addr;
} DRAM_pte;

typedef struct {
	DRAM_pte *lst;
	size_t len;
} RowMap;

typedef struct {
	size_t base_row;	// used as an offset
	RowMap *row_maps;
} ADDRMapper;

void init_addr_mapper(ADDRMapper * mapper, MemoryBuffer * mem,
		      DRAMAddr * d_base, size_t h_rows);
RowMap get_row_map(ADDRMapper * mapper, DRAMAddr * d_addr);
DRAM_pte get_dram_pte(ADDRMapper * mapper, DRAMAddr * d_addr);
void tear_down_addr_mapper(ADDRMapper * mapper);

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define STRIPE_SHIFT	0	// not needed anymore
#define O2Z 			(0b01 << STRIPE_SHIFT)	// ONE_TO_ZERO
#define Z2O 			(0b10 << STRIPE_SHIFT)	// ZERO_TO_ONE
#define REVERSE_VAL 	(O2Z ^ Z2O)	// if you xor REVERSE with one of the stripe val it will give you the opposite

static const char *config_str[] =
    { "assisted-dbl", "free-triple", "%i_sided"};
static const char *data_str[] = { "random", "i2o", "o2i" };

typedef enum {
	ASSISTED_DOUBLE_SIDED,
	FREE_TRIPLE_SIDED,
	N_SIDED,
} HammerConfig;

typedef enum {
	RANDOM,
	ONE_TO_ZERO = O2Z,
	ZERO_TO_ONE = Z2O,
	REVERSE = REVERSE_VAL
} HammerData;

typedef uint64_t physaddr_t;

/*	not necessarily page-aligned addresses.
	used only to keep track of virt<->phys mapping. */
typedef struct {
	char *v_addr;
	physaddr_t p_addr;
} pte_t;

typedef struct {
	HammerConfig h_cfg;
	HammerData d_cfg;
	size_t h_rows;
	size_t h_rounds;
	size_t base_off;	// offset from the beginning of the contig chunk
	int	   aggr_n;
} SessionConfig;

typedef struct {
	char *buffer;		// base addr
	pte_t *physmap;		// list of virt<->phys mapping for every page
	int fd;				// fd in the case of mmap hugetlbfs
	uint64_t size;		// in bytes
	uint64_t align;
	uint64_t flags;		// from params
} MemoryBuffer;

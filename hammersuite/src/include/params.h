/*
 * Copyright (c) 2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef PARAMS_H
#define PARAMS_H 1

#include <stddef.h>
#include <stdint.h>

#define ROUNDS_std      1000000
#define HUGETLB_std     "/mnt/huge/buff"
#define CONFIG_NAME_std "tmp/s_cfg.bin"
#define O_FILE_std      "DIMM00"
#define ALLOC_SIZE     	1<<30
#define ALIGN_std       2<<20
#define PATT_LEN 		1024
#define AGGR_std		2
#define HUGE_YES

typedef struct ProfileParams {
	uint64_t g_flags 		= 0;
	char 	*g_out_prefix;
	char	*tpat			= (char *)NULL;
	char 	*vpat			= (char *)NULL;
	int		 threshold		= 0;
	int 	fuzzing         = 0;		// start fuzzing!!
	size_t   m_size			= ALLOC_SIZE;
	size_t   m_align 		= ALIGN_std;
	size_t   rounds			= ROUNDS_std;
	size_t   base_off 		= 0;
	char     *huge_file		= (char *)HUGETLB_std;
	int		 huge_fd;
	char     *conf_file		= (char *)CONFIG_NAME_std;
	int 	 aggr			= AGGR_std;
} ProfileParams;

int process_argv(int argc, char *argv[], ProfileParams *params);

#endif /* params.h */

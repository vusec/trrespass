/*
 * Copyright (c) 2016 Andrei Tatar
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "include/params.h"
#include "include/utils.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <getopt.h>

void print_usage(char *bin_name)
{
	fprintf(stderr,
		"[ HELP ] - Usage ./%s [-h] [-r rounds] [-a aggr] [-o o_file] [-v] [--mem mem_size] [--[huge/HUGE] f_name] [--conf f_name] [--align val] [--off val] [--no-overwrite] [--fuzzing]\n",
		bin_name);
	fprintf(stderr, "\t-h\t\t\t= this help message\n");
	fprintf(stderr, "\t-v\t\t\t= verbose\n\n");
	fprintf(stderr, "\t-r rounds\t\t= number of rounds per tuple\t\t\t(default: %d)\n", ROUNDS_std);
	fprintf(stderr, "\t-a --aggr\t\t= number of aggressors\t\t\t\t(default: %d)\n", AGGR_std);
	fprintf(stderr,	"\t-o --o_file_prefix\t= prefix for output files\t\t\t(default: %s)\n", O_FILE_std);
	fprintf(stderr, "\t--mem mem_size\t\t= allocation size\t\t\t\t(default: %ld)\n",
		(uint64_t) ALLOC_SIZE);
	fprintf(stderr, "\t--huge f_name\t\t= hugetlbfs entry (1GB if HUGE)\t\t\t(default: %s)\n",
		HUGETLB_std);
	fprintf(stderr, "\t--conf f_name\t\t= SessionConfig file\t\t\t\t(default: %s)\n",
		CONFIG_NAME_std);
	fprintf(stderr, "\t--align val\t\t= alignment of the buffer\t\t\t(default: %ld)\n",
		(uint64_t) ALIGN_std);
	fprintf(stderr, "\t--off val\t\t= offset from first row\t\t\t\t(default: 0)\n");
	fprintf(stderr, "\t--no-overwrite\t\t= don't overwrite previous file\n");
	fprintf(stderr, "\t-V --victim-pattern\t= hex value for the victim patter\n");
	fprintf(stderr, "\t-T --target-pattern\t= hex value for the target pattern\n");
	fprintf(stderr, "\t-f --fuzzing\t\t= Start fuzzing (--aggr will be ignored)\n");
	fprintf(stderr, "\t-t --threshold\t\t= Align the hammering to refresh ops,\n\t\t\t\t looking at the memory latency in CPU cycles.\t(default: 0)\n");
}

static int str2pat(const char *str, char **pat)
{
	char *endp = NULL;
	char tmp[3];
	char *p;
	tmp[2] = '\0';
	size_t len = strlen(str);
	if (len % 2) {
		return EINVAL;
	}
	len /= 2;

	p = (char *)malloc(len);
	for (size_t i = 0; i < len; i++) {
		tmp[0] = str[2 * i];
		tmp[1] = str[2 * i + 1];
		errno = 0;
		((uint8_t *) p)[i] = (uint8_t) strtol(tmp, &endp, 16);
		if (errno) {
			free(p);
			return errno;
		}
		if (*endp != '\0') {
			free(p);
			return EINVAL;
		}
	}
	*pat = p;
	return 0;
}

int process_argv(int argc, char *argv[], ProfileParams *p)
{

	/* Default */
	p->g_flags   = 0;
	p->tpat      = (char *)NULL;
	p->vpat      = (char *)NULL;
	p->threshold = 0;
	p->fuzzing   = 0;		// start fuzzing!!
	p->m_size    = ALLOC_SIZE;
	p->m_align   = ALIGN_std;
	p->rounds    = ROUNDS_std;
	p->base_off  = 0;
	p->huge_file = (char *)HUGETLB_std;
	p->conf_file = (char *)CONFIG_NAME_std;
	p->aggr      = AGGR_std;


	const struct option long_options[] = {
		/* These options set a flag. */
		{"mem", required_argument, 0, 0},
		{"align", required_argument, 0, 0},
		{"huge", optional_argument, 0, 0},
		{"HUGE", optional_argument, 0, 0},
		{"conf", optional_argument, 0, 0},
		{"off", required_argument, 0, 0},
		{"no-overwrite", no_argument, 0, 0},
		{.name = "target-pattern",.has_arg = required_argument,.flag = NULL,.val='T'},
		{.name = "victim-pattern",.has_arg = required_argument,.flag = NULL,.val = 'V'},
		{.name = "aggr",.has_arg = required_argument,.flag = NULL,.val='a'},
		{.name = "fuzzing",.has_arg = no_argument,.flag = &p->fuzzing,.val = 1},
		{.name = "threshold",.has_arg = required_argument,.flag = NULL,.val = 't'},
		{0, 0, 0, 0}
	};

	p->g_out_prefix = (char *)O_FILE_std;
	p->g_flags |= F_POPULATE;

	while (1) {
		int this_option_optind = optind ? optind : 1;
		int option_index = 0;
		int arg = getopt_long(argc, argv, "o:d:r:hvV:T:a:ft:",
				      long_options, &option_index);

		if (arg == -1)
			break;

		switch (arg) {
		case 0:
			switch (option_index) {
			case 0:
				p->m_size = atoi(optarg);
				break;
			case 1:
				p->m_align = atoi(optarg);
				break;
			case 2:
				p->g_flags |= F_ALLOC_HUGE_2M;
			case 3:
				p->huge_file = (char *)malloc(sizeof(char) * strlen(optarg));
				strncpy(p->huge_file, optarg, strlen(optarg));
				p->g_flags |= F_ALLOC_HUGE_1G;
				break;
			case 4:
				p->g_flags |= F_CONFIG;
				if (!optarg)
					break;
				p->conf_file = (char *)malloc(sizeof(char) * strlen(optarg));
				strncpy(p->conf_file, optarg, strlen(optarg));
				break;
			case 5:
				p->base_off = atoi(optarg);
				break;
			case 6:
				p->g_flags |= F_NO_OVERWRITE;
				break;
			default:
				break;
			}
			break;
		case 'o':
			p->g_out_prefix = (char *)malloc(sizeof(char) * strlen(optarg));
			strncpy(p->g_out_prefix, optarg, strlen(optarg));
			p->g_flags |= F_EXPORT;
			break;
		case 'r':
			p->rounds = atoi(optarg);
			break;
		case 'v':
			p->g_flags |= F_VERBOSE;
			break;
		case 'a':
			p->aggr = atoi(optarg);
			break;
		case 'T':
			if (str2pat(optarg, &(p->vpat))) {
				fprintf(stderr, "Invalid target fill pattern: %s\n", optarg);
				return -1;
			}
			break;
		case 'V':
			if (str2pat(optarg, &(p->tpat))) {
				fprintf(stderr, "Invalid victim fill pattern: %s\n", optarg);
				return -1;
			}
			break;
		case 'f':
			p->fuzzing = 1;
			break;
		case 't':
			p->threshold = atoi(optarg);
			break;
		case 'h':
		default:
			print_usage(argv[0]);
			return -1;
		}
	}
#ifdef HUGE_YES
	p->g_flags |= F_ALLOC_HUGE_1G;
#endif

	if (p->g_flags & (F_ALLOC_HUGE_2M | F_ALLOC_HUGE_1G)) {
		if ((p->huge_fd = open(p->huge_file, O_CREAT | O_RDWR, 0755)) == -1) {
			perror("[ERROR] - Unable to open hugetlbfs");
			return -1;
		}
	}
	return 0;
}

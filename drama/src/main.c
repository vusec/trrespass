
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

#include "utils.h"
#include "rev-mc.h"


#define SETS_std        (2*16) // 1rk-1ch
#define ROUNDS_std      1000
#define THRESHOLD_std   340
#define MEM_SIZE_std    GB(5L)
#define O_FILE_std      "access.csv"


#define FIELDS "base,probe,rounds,time"


//-----------------------------------------------
//                  GLOBALS



//-----------------------------------------------
void print_usage() {
    fprintf(stderr, "[ LOG ] - Usage ./test [-h] [-s sets] [-r rounds] [-t threshold] [-o o_file] [-v] [--mem mem_size]\n");
    fprintf(stderr, "          -h                     = this help message\n");
    fprintf(stderr, "          -s sets                = number of expected sets            (default: %d)\n", SETS_std);
    fprintf(stderr, "          -r rounds              = number of rounds per tuple         (default: %d)\n", ROUNDS_std);
    fprintf(stderr, "          -t threshold           = time threshold for conflicts       (default: %d)\n", THRESHOLD_std);
    fprintf(stderr, "          -o o_file              = output file for mem profiling      (default: %s)\n", O_FILE_std);
    fprintf(stderr, "          --mem mem_size         = allocation size                    (default: %ld)\n", (uint64_t) MEM_SIZE_std);
    fprintf(stderr, "          -v                     = verbose\n\n");
}










//-----------------------------------------------
int main(int argc, char** argv) {

    uint64_t    flags       = 0ULL;
    size_t      sets_cnt    = SETS_std;
    size_t      rounds      = ROUNDS_std;
    size_t      m_size      = MEM_SIZE_std;
    size_t      threshold   = THRESHOLD_std;
    char*       o_file      = (char*) O_FILE_std;

    flags |= F_POPULATE;
	
    if(geteuid() != 0) {
    	fprintf(stderr, "[ERROR] - You need to run as root to access pagemap!\n");
	exit(1);
    }

    while (1) {
        int this_option_optind = optind ? optind : 1;
                      int option_index = 0;
        static struct option long_options[] =
            {
              /* These options set a flag. */
              {"mem",   required_argument,       0, 0},
              {0, 0, 0, 0}
            };
        int arg = getopt_long (argc, argv, "o:s:r:t:hv",
                       long_options, &option_index);

        if (arg == -1) 
            break;

        switch(arg) {
            case 0:
                switch (option_index){
                    case 0:
                        m_size = atoi(optarg); // TODO proper parsing of this
                        break;
                    default:
                        break;
                }
                break;
            case 'o':   
                o_file = (char*) malloc(sizeof(char)*strlen(optarg));
                strncpy(o_file, optarg, strlen(optarg));
                flags |= F_EXPORT;
                break;
            case 's':
                sets_cnt = atoi(optarg); 
                break;  
            case 'r':
                rounds = atoi(optarg);
                break;
            case 't':
                threshold = atoi(optarg);
                break;
            case 'v':
                flags |= F_VERBOSE;
                break;
            case 'h':
            default:
                print_usage();
                return 0;   
            
        }
    }



    rev_mc(sets_cnt, threshold, rounds, m_size, o_file, flags);
    return 0;

}




/*I'm refactoring the set struct to add also timing in there. */

#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <inttypes.h>
#include <string.h>


#include <vector>
#include <functional>
#include <algorithm>
#include <bitset>  

#include "rev-mc.h"

#define BOOL_XOR(a,b) ((a) != (b))
#define O_HEADER "base,probe,time\n"
#define ALIGN_TO(X, Y) ((X) & (~((1LL<<(Y))-1LL))) // Mask out the lower Y bits
#define LS_BITMASK(X)  ((1LL<<(X))-1LL) // Mask only the lower X bits

#define SET_SIZE 40 // elements per set 
#define VALID_THRESH    0.75f
#define SET_THRESH      0.95f
#define BITSET_SIZE 256  // bitset used to exploit bitwise operations 
#define ROW_SET_CNT 5

// from https://stackoverflow.com/questions/1644868/define-macro-for-debug-printing-in-c
#define verbose_printerr(fmt, ...) \
	do { if (flags & F_VERBOSE) { fprintf(stderr, fmt, ##__VA_ARGS__); } } while(0)



typedef std::vector<addr_tuple> set_t; 

//-------------------------------------------
bool is_in(char* val, std::vector<char*> arr);
bool found_enough(std::vector<set_t> sets, uint64_t set_cnt, size_t set_size);
void filter_sets(std::vector<set_t>& sets, size_t set_size);
void print_sets(std::vector<set_t> sets);
void verify_sets(std::vector<set_t>& sets, uint64_t threshold, size_t rounds);

//-------------------------------------------
uint64_t time_tuple(volatile char* a1, volatile char* a2, size_t rounds) {

    uint64_t* time_vals = (uint64_t*) calloc(rounds, sizeof(uint64_t));
    uint64_t t0;
    sched_yield();
    for (size_t i = 0; i < rounds; i++) {
        mfence();
        t0 = rdtscp();
        *a1;
        *a2;
        time_vals[i] = rdtscp() - t0; 
        lfence();
        clflush(a1);
        clflush(a2);

    }

    uint64_t mdn = median(time_vals, rounds);
    free(time_vals);
    return mdn;
}



//----------------------------------------------------------
char* get_rnd_addr(char* base, size_t m_size, size_t align) {
        return (char*) ALIGN_TO((uint64_t) base, (uint64_t) align) + ALIGN_TO(rand() % m_size, (uint64_t) align);
}



//----------------------------------------------------------
uint64_t get_pfn(uint64_t entry) {
    return ((entry) & 0x3fffffffffffff);
}

//----------------------------------------------------------
uint64_t get_phys_addr(uint64_t v_addr) 
{
    uint64_t entry; 
    uint64_t offset = (v_addr/4096) * sizeof(entry);
    uint64_t pfn; 
    int fd = open("/proc/self/pagemap", O_RDONLY);
    assert(fd >= 0);
    int bytes_read = pread(fd, &entry, sizeof(entry), offset);
    close(fd);
    assert(bytes_read == 8);
    assert(entry & (1ULL << 63));
    pfn = get_pfn(entry);
    assert(pfn != 0);
    return (pfn*4096) | (v_addr & 4095); 
}


//----------------------------------------------------------
addr_tuple gen_addr_tuple(char* v_addr) {
    return (addr_tuple) { v_addr, get_phys_addr((uint64_t) v_addr)};
}




//----------------------------------------------------------
// https://www.cs.umd.edu/~gasarch/TOPICS/factoring/fastgauss.pdf
// gaussian elimination in GF2 

std::vector<uint64_t> reduce_masks(std::vector<uint64_t> masks) {

    size_t height, width, height_t, width_t;

    height = masks.size();
    width = 0;
    for (auto m:masks) {
        uint64_t max_one = 64 - __builtin_clzl(m);
        width = (max_one > width)? max_one:width;
    }
    
    height_t = width;
    width_t = height;

    std::vector<std::vector<bool>> mtx(height, std::vector<bool>(width));
    std::vector<std::vector<bool>> mtx_t(height_t, std::vector<bool>(width_t)); 
    std::vector<uint64_t> filtered_masks;

    for (size_t i =0; i<height;i++) {
        for (size_t j=0; j<width; j++) {
            mtx[i][width - j - 1] = (masks[i] & (1ULL<<(j)));
        }
    }

    for (size_t i =0; i<height;i++) {   
        for (size_t j=0; j<width; j++) {
            mtx_t[j][i] = mtx[i][j];
        }
    }

    int64_t pvt_col = 0;

    while (pvt_col < width_t) {
        for (uint64_t row = 0; row < height_t; row++) {
            if (mtx_t[row][pvt_col]) {
                filtered_masks.push_back(masks[pvt_col]);
                for (size_t c=0; c<width_t; c++) {
                    if (c == pvt_col)
                        continue;
                    if (!(mtx_t[row][c]))
                        continue;

                    // column sum
                    for (size_t r=0; r<height_t; r++) {
                        mtx_t[r][c] = BOOL_XOR(mtx_t[r][c], mtx_t[r][pvt_col]); 
                    }  

                }
                break;
            }
        }
        pvt_col++;
    }

    return filtered_masks;

}



//----------------------------------------------------------
// from https://graphics.stanford.edu/~seander/bithacks.html#NextBitPermutation
uint64_t next_bit_permutation(uint64_t v) {
        uint64_t t = v | (v - 1);
        return (t + 1) | (((~t & -~t) - 1) >> (__builtin_ctzl(v) + 1));
}



//----------------------------------------------------------
std::vector<uint64_t> find_functions(std::vector<set_t> sets, size_t max_fn_bits, size_t msb, uint64_t flags) {

    std::vector<uint64_t> masks;
    verbose_printerr("~~~~~~~~~~ Candidate functions ~~~~~~~~~~\n");
    
    for (size_t bits = 1L; bits <= max_fn_bits; bits++) {
        uint64_t fn_mask = ((1L<<(bits))-1); // avoid the first 6 bits since they are the cacheline bits
        uint64_t last_mask = (fn_mask<<(msb-bits));
    	fn_mask <<= CL_SHIFT;
        verbose_printerr("[ LOG ] - #Bits: %ld \n", bits);
        while (fn_mask != last_mask) {
            if (fn_mask & LS_BITMASK(6)){
                fn_mask = next_bit_permutation(fn_mask);
                continue;
            }
            for (size_t idx = 0; idx<sets.size(); idx++) {
                set_t curr_set = sets[idx];
                size_t inner_cnt = 0;
                for (size_t i = 1; i < curr_set.size(); i++) {
                    uint64_t res_base = __builtin_parityl(curr_set[0].p_addr & fn_mask);
                    uint64_t res_probe = __builtin_parityl(curr_set[i].p_addr & fn_mask);
                    if (res_base != res_probe) {
                        goto next_mask;
                    }
                }
            }
        verbose_printerr("\t Candidate: 0x%0lx \t\t bits: %s\n", fn_mask, bit_string(fn_mask));
        masks.push_back(fn_mask);    
                 
        next_mask:
        fn_mask = next_bit_permutation(fn_mask);
        }
    }
    verbose_printerr("~~~~~~~~~~ Found Functions ~~~~~~~~~~\n");
    masks = reduce_masks(masks);
    if (flags & F_VERBOSE) {
	for (auto m: masks) {
        	fprintf(stderr, "\t Valid Function: 0x%0lx \t\t bits: %s\n", m, bit_string(m));
    	}    
    }
    for (auto m: masks) {
	fprintf(stdout, "0x%lx\n", m);
    }
    return masks;

}


std::vector<int> find_set_bits(uint64_t val) {
    std::vector<int> set_bits;
    for (int i = 0; i<64; i++) {
            if (!(val & (1ULL << i)))
                continue;

            set_bits.push_back(i);
        }
    return set_bits;
}

//----------------------------------------------------------
std::vector<uint8_t> get_dram_fn(uint64_t addr, std::vector<uint64_t> fn_masks) {
    std::vector<uint8_t> addr_dram;
    for (auto fn:fn_masks) {
        addr_dram.push_back(__builtin_parityl( addr & fn));
    }
    return addr_dram;
}

//----------------------------------------------------------
/* 
It currently finds some of the interesting bits for the row addressing. 
@TODO 	still need to figure out which bits are used for the row addressing and which 
	are from the bank selection. This is currently done manually 
*/
uint64_t find_row_mask(std::vector<set_t>& sets, std::vector<uint64_t> fn_masks, mem_buff_t mem, uint64_t threshold, uint64_t flags) {



    addr_tuple base_addr = gen_addr_tuple(get_rnd_addr(mem.buffer, mem.size, 0));
    std::vector<set_t> same_row_sets;

    verbose_printerr("~~~~~~~~~~ Looking for row bits ~~~~~~~~~~\n");


    for (int i = 0; i < 2; i++) {
        verbose_printerr("[LOG] - Set #%d\n", i);
        addr_tuple base_addr = sets[i][0];
        std::vector<uint8_t> base_dram = get_dram_fn((uint64_t)base_addr.p_addr, fn_masks);
        same_row_sets.push_back({base_addr});
        uint64_t cnt = 0;
        while (cnt < ROW_SET_CNT) {

            addr_tuple tmp = gen_addr_tuple(get_rnd_addr(mem.buffer, mem.size, 0));
            if (get_dram_fn((uint64_t) tmp.p_addr, fn_masks) != base_dram) 
                continue;

            uint64_t time = time_tuple((volatile char*)base_addr.v_addr, (volatile char*)tmp.v_addr, 1000);
            
            if (time > threshold) 
		continue;

            
	    verbose_printerr("[LOG] - %lx - %lx\t Time: %ld <== GOTCHA\n", base_addr.p_addr, tmp.p_addr, time);
            
            same_row_sets[i].push_back(tmp);
            cnt++;            
        }
    }
    
    


    uint64_t row_mask = LS_BITMASK(16); // use 16 bits for the row
    uint64_t last_mask = (row_mask<<(40-16));
    row_mask <<= CL_SHIFT; // skip the lowest 6 bits since they're used for CL addressing

    while (row_mask < last_mask) {
        if (row_mask & LS_BITMASK(CL_SHIFT)){
                row_mask = next_bit_permutation(row_mask);
                continue;
        }

        for (auto addr_pool:same_row_sets) {
            addr_tuple base_addr = addr_pool[0];
            for (int i = 1; i < addr_pool.size(); i++) {
                addr_tuple tmp = addr_pool[i];
                if ((tmp.p_addr & row_mask) != (base_addr.p_addr & row_mask)) {
                    goto next_mask;
                }
            }
    
        }
        
        break;

        next_mask:
        row_mask = next_bit_permutation(row_mask);
    }
  	
   // super hackish way to recover the real row mask  
    for (auto m:fn_masks) {
	uint64_t lsb = (1<<(__builtin_ctzl(m)+1));
	if (lsb & row_mask) {
    		row_mask ^= (1<<__builtin_ctzl(m));
	}
    }
    verbose_printerr("[LOG] - Row mask: 0x%0lx \t\t bits: %s\n", row_mask, bit_string(row_mask));	
    printf("0x%lx\n", row_mask);

}


//----------------------------------------------------------
void rev_mc(size_t sets_cnt, size_t threshold, size_t rounds, size_t m_size, char* o_file, uint64_t flags) {

    time_t t;

    int o_fd = 0;
    int huge_fd = 0;
    std::vector<set_t> sets;
    std::vector<char*> used_addr;
    std::vector<uint64_t> fn_masks;

    srand((unsigned) time(&t));

    if (flags & F_EXPORT) {
        if (o_file == NULL) {
            fprintf(stderr, "[ERROR] - Missing export file name\n");
            exit(1);
        }
        if((o_fd = open(o_file, O_CREAT|O_RDWR)) == -1) {
            perror("[ERROR] - Unable to create export file");
            exit(1);
        }
    dprintf(o_fd, O_HEADER);
    }

    mem_buff_t mem = {
        .buffer = NULL,
        .size   = m_size,
        .flags  = flags ,
    };

    alloc_buffer(&mem);


    while (!found_enough(sets, sets_cnt, SET_SIZE)) {
        char* rnd_addr = get_rnd_addr(mem.buffer, mem.size, CL_SHIFT);
        if (is_in(rnd_addr, used_addr))
            continue;

        used_addr.push_back(rnd_addr);

        addr_tuple tp = gen_addr_tuple(rnd_addr);
        bool found_set = false;
        for (size_t idx = 0; idx < sets.size(); idx++) {
            uint64_t time = 0;
            addr_tuple tmp = sets[idx][0];
            time = time_tuple((volatile char*) tmp.v_addr, (volatile char*)tp.v_addr, rounds);
        if (flags & F_EXPORT) {
            dprintf(o_fd, "%lx,%lx,%ld\n",(uint64_t) tp.v_addr, (uint64_t) tmp.v_addr,time);
        }
            if (time > threshold) {
                verbose_printerr("[LOG] - [%ld] Set: %03ld -\t %lx - %lx\t Time: %ld\n", used_addr.size(), idx, tp.p_addr, tmp.p_addr, time);
                sets[idx].push_back(tp);
                found_set = true;
                break;
            }
        }
        if (!found_set) {
            sets.push_back({tp});
            verbose_printerr( "[LOG] - Set: %03ld -\t %p                                    <== NEW!!\n", sets.size(), tp.v_addr);
        }

    }

    filter_sets(sets, SET_SIZE);

#ifdef DEBUG_SETS
    fprintf(stderr, "[ LOG ] - Cleansing sets. This may take a while... stay put\n");
    verify_sets(sets, threshold, rounds);
    fprintf(stderr, "[ LOG ] - Done\n");    
#endif     

    if (flags & F_VERBOSE) {
        print_sets(sets);
    }

    fn_masks = find_functions(sets, 6, 30, flags);
    uint64_t row_mask = find_row_mask(sets, fn_masks, mem, threshold, flags);

    free_buffer(&mem);
}



// Fin.

//----------------------------------------------------------
//          Helpers

bool is_in(char* val, std::vector<char*> arr) {
    for (auto v: arr) {
        if (val == v) {
            return true;
        }
    }
    return false;
}

//----------------------------------------------------------
bool found_enough(std::vector<set_t> sets, uint64_t set_cnt, size_t set_size) {

    size_t found_sets = 0;

    for (int i =0; i < sets.size(); i++) {
        set_t curr_set = sets[i];
        if (curr_set.size() > set_size) {
            found_sets += 1;
        }
    }

    if (found_sets > set_cnt) {
        fprintf(stderr, "[ERROR] - Found too many sets. Is %ld the correct number of sets?\n", set_cnt);
        exit(1);
    } 

    return (found_sets >= (set_cnt * SET_THRESH)) ? true : false;
}


void filter_sets(std::vector<set_t>& sets, size_t set_size) {

    for (auto s = sets.begin(); s < sets.end(); s++) {
        if (s->size() < set_size) {
            sets.erase(s);
            s -= 1;
        }
    }
}


void print_sets(std::vector<set_t> sets) {

    for (int idx = 0; idx < sets.size(); idx++) {
        fprintf(stderr, "[LOG] - Set: %d\tSize: %ld\n", idx, sets[idx].size());
        for (auto tmp: sets[idx]) {
            fprintf(stderr, "\tv_addr:%p - p_addr:%p\n", tmp.v_addr, (void*) tmp.p_addr);
        }
    }    
}

#ifdef DEBUG_SETS

void verify_sets(std::vector<set_t>& sets, uint64_t threshold, size_t rounds) {

    for (auto s: sets) {
        // test every address against all the addresses in the set 
        for (auto tp_base = s.begin(); tp_base < s.end(); tp_base++) {
            uint64_t conflicts = 0;
            for (auto tp_probe = s.begin(); tp_probe < s.end(); tp_probe++) {
                if (tp_base == tp_probe)
                    continue;

                uint64_t time = time_tuple((volatile char*) tp_base->v_addr,(volatile char*) tp_probe->v_addr, rounds);
                if (time>threshold){
                    conflicts += 1;
                }
            }
            if (!(conflicts > VALID_THRESH*s.size())) {
                fprintf(stderr, "[ LOG ] - Removing: %p\n", tp_base->v_addr);
                s.erase(tp_base--); // reset the iterator
            }
        }
    }
}

#endif 


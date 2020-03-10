# TRRespass

## About
TRRespass code implements several tests to reveal the presence of the Rowhammer vulnerability.
Since several defenses may be in place in the memory controller and/or at DRAM level, TRRespass tests for non-conventional hammering patterns that may be able to bypass defenses. 


## Requirements
### Physical to DRAM mapping functions
The functions resolving the mapping of a physical address into a DRAM address are kept in the following structures:

`include/types.h:`

```c
typedef struct {
    uint64_t lst[HASH_FN_CNT];
    uint64_t len;
} AddrFns;

typedef struct {
    AddrFns h_fns;
    uint64_t row_mask;
    uint64_t col_mask;
} DRAMLayout
```

The mapping functions must be defined in ```main.c```:
```c
DRAMLayout g_mem_layout = {{{0x4080,0x48000,0x90000,0x120000,0x1b300}, 5}, row_mask, ROW_SIZE-1};
```

AMD publicly documents mapping functions in th "BIOS and Kernel Developer’s Guide (BKDG)"
Contrariwise, Intel does not. We provide a tool to retrieve the mapping functions, based on the techniques described in [1]. It's a bit hackish but it works.
The tool is available in the folder ./drama (read the README in the folder).

### Huge pages support
1GB Huge Page support is required to gain physically continuis memory and perform templating.
 
## Usage
Commands must be run with `sudo` privileges.

- Full options list
  
```
sudo ./obj/test --help
```

- Common usage

```
sudo ./obj/tester -r 1000000 -v -V ff -T 00 --no-overwrite -o DIMM00
```

1. By default double-sided  RH is tested.
2. `-r`  indicates the number of accesses for each target row.
3. The options `-V `and `-T` specify the victim row(s) and the target rows data pattern. By default the data pattern is randomly generated.  
4. The `-o` argument allow to specify a prefix for the output file. `--no-overwrite` avoids to overwrite an output file with the same name if present.
5. Testing for non-conventional hammering patterns (i.e., black-box fuzzing)
   
```
sudo ./obj/tester -v --fuzzing
```

This will test the RH vulnerability against randomly generated hammering patterns.

At the moment the tool exports the results in files we call Fliptables (the export choice is currently hardcoded as a #define). You can use `hammerstats.py` in the `../py` folder to print out statistics about the number of bit flips. 
The format is not so human friendly but it was helping us to print out statistics using some pre-existing toolchains we had. 


#### References

[1] "DRAMA: Exploiting DRAM Addressing for Cross-CPU Attacks", Usenix Sec 16, Pessl et al.
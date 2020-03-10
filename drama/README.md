# DRAMA

This tool can be used to reverse engineer the DRAM mapping functions of the test system.
It carries out two main tasks: 

- Recovering the bank conflicts functions 
- Recovering the row address function 

The tool is a bit hackish since it makes some strong assumptions on the bits used for different purposes. 
For instance it assumes that the bits from the physical address used to select the row are among the high bits. 
This seems to be a valid assumption on all the Intel consumer platforms we tested. 
However on server platforms and AMD ryzen machines it doesn't yield great results. 
Once recovered the functions these can be used in `hammersuite` as explained in the the other README. 

## Usage

```
./test [-h] [-s sets] [-r rounds] [-t threshold] [-o o_file] [-v] [--mem mem_size]
          -h                     = this help message
          -s sets                = number of expected sets            (default: 32)
          -r rounds              = number of rounds per tuple         (default: 1000)
          -t threshold           = time threshold for conflicts       (default: 340)
          -o o_file              = output file for mem profiling      (default: access.csv)
          --mem mem_size         = allocation size                    (default: 5368709120)
          -v                     = verbose
```

We recommend running it as verbose (`-v`) to get more insights on what's going on. 
Otherwise it will simply output to stdout first the bank conflicts functions and then the row address mask.  
 
**Number of sets:**

- The number of expected sets is defined by the memory configuration. For instance in a common dual-rank, single-channel configuration you would expect 32 banks (i.e., sets) in total.  You can pass any value you want to the script. If this value is unknown 16 is usually a safe bet.  

**Time threshold:**

- You can identify the time threshold by running the tool the first time with `-o` and plotting the results with the histogram.py script available in the repo. Once you know the threshold you can dinamycally pass it to the binary. 

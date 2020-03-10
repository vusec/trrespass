# TRRespass 

This is the repository for the TRRespass Rowhammer fuzzer. Recent DDR4 chips include on-chip TRR mitigations that stop bit flips using standard Rowhammer access patterns such as double-sided, single sided or one-location hammering. TRRespass automatically discovers novel Many-sided Rowhammer variants that can bypass these mitigations and trigger bit flips on the recent systems with DDR4 memory. TRRespass requires the DRAM address mapping functions to work effectively.

Additional information about TRRespass can be found here: https://www.vusec.net/projects/trrespass/

The paper that describes more details appears at IEEE Security and Privacy 2020 and can be found here: https://download.vusec.net/papers/trrespass_sp20.pdf

### ./drama

Inside the `drama` folder you can find a tool that helps you reverse engineer the DRAM memory mappings used by the memory controller. 
Read the README in the folder for more details 

### ./hammersuite

Inside the `hammersuite` folder you can find the fuzzer we used.  
Again, read the README in the folder for more details

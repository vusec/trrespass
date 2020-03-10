#pragma once

#include "types.h"
#include <stddef.h>

void set_physmap(MemoryBuffer * mem);
physaddr_t virt_2_phys(char *v_addr, MemoryBuffer * mem);
char *phys_2_virt(physaddr_t p_addr, MemoryBuffer * mem);

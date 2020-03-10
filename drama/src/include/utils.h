#pragma once 

#include <stdint.h>
#include <stdio.h>


#define BIT(x) (1ULL<<(x))
#define KB(x) ((x)<<10ULL)
#define MB(x) ((x)<<20ULL)
#define GB(x) ((x)<<30ULL)
#define CL_SHIFT 6
#define CL_SIZE 64


#define F_CLEAR 	0L
#define F_VERBOSE 	BIT(0)
#define F_EXPORT 	BIT(1)

#define MEM_SHIFT			(30L)
#define MEM_MASK			0b11111ULL << MEM_SHIFT				
#define F_ALLOC_HUGE 		BIT(MEM_SHIFT)
#define F_ALLOC_HUGE_1G 	F_ALLOC_HUGE | BIT(MEM_SHIFT+1)
#define F_ALLOC_HUGE_2M		F_ALLOC_HUGE | BIT(MEM_SHIFT+2)
#define F_POPULATE			BIT(MEM_SHIFT+3)



//----------------------------------------------------------
// 			Static functions

static inline __attribute__((always_inline)) void clflush(volatile void *p)
{
	asm volatile("clflush (%0)\n"
		:: "r" (p) : "memory");
}


static inline __attribute__((always_inline)) void mfence() 
{
	asm volatile ("mfence" : : : "memory");
}


static inline __attribute__((always_inline)) void lfence() 
{
	asm volatile ("lfence" : : : "memory");
}


static inline __attribute__((always_inline)) uint64_t rdtscp(void)
{
	uint64_t lo, hi;
	asm volatile("rdtscp\n"
		: "=a" (lo), "=d" (hi)
		:: "%rcx");
	return (hi << 32) | lo;
}


static inline __attribute__((always_inline)) uint64_t rdtsc(void)
{
	uint64_t lo, hi;
	asm volatile("rdtsc\n"
		: "=a" (lo), "=d" (hi)
		:: "%rcx");
	return (hi << 32) | lo;
}


//----------------------------------------------------------
// 			Memory alloc


typedef struct {
	char* 		buffer;
	uint64_t 	size;
	uint64_t 	flags; 
} mem_buff_t;


int alloc_buffer(mem_buff_t* mem);

int free_buffer(mem_buff_t* mem);


//----------------------------------------------------------
// 			Helpers 
int gt(const void * a, const void * b);

double mean(uint64_t* vals, size_t size);

uint64_t median(uint64_t* vals, size_t size);

char* bit_string(uint64_t val);


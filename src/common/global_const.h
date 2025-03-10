/*
 * 2015-xxx by YuJie Chen
 * email:    YuJieChen_hust@163.com
 * function: extend zsim-nvmain with some other simulation,such as tlb,page table,page table,etc.  
 */

/*
 * Copyright (C) 2020 Chao Yu (yuchaocs@gmail.com)
 */

#ifndef GLOBAL_CONST_H
#define GLOBAL_CONST_H

#include <list>
#include "stdint.h"
#define MAXLEN 1024
#define SWAP_CLUSTER_MAX (32ULL)
#define MAXORDER 11
#define PAGE_BUDDY_MAPCOUNT_VALUE (-128)		//if page is belong to buddy system, its mapcount should be -128

/* Addresses are plain 64-bit uints. This should be kept compatible with PIN addrints */
typedef uint64_t Address;

#define PAGE_FAULT_SIG ((Address)(-1))
#define INVALID_PAGE_ADDR ((Address)(-1))
#define INVALID_PROC ((uint32_t)(-1))
//default page size is 4KB
const unsigned PAGE_SHIFT=12;
const unsigned PAGE_2MB_SHIFT=20;
const unsigned PAGE_4MB_SHIFT=22;
const unsigned PAGE_1GB_SHIFT=30;
const unsigned PAGE_SIZE=(1UL<<PAGE_SHIFT);
const unsigned PAGE_2MB=(1ULL<<PAGE_2MB_SHIFT);
const unsigned PAGE_4MB=(1ULL<<PAGE_4MB_SHIFT);
const unsigned PAGE_1GB=(1ULL<<PAGE_1GB_SHIFT);
//const unsigned PAGE_MASK=(~(PAGE_SIZE-1));
const unsigned ENTRY_1024=1024;
const unsigned ENTRY_4=4;
const unsigned ENTRY_512=512;
const unsigned ENTRY_SHIFT_512=3;
const unsigned ENTRY_SIZE_512=(1UL<<ENTRY_SHIFT_512);
const unsigned MAXPN_LEN=52;
const char* const c_zone_sec="mem.zone";
const char* const c_zone_dma="mem.zone.zone_dma";
const char* const c_zone_dma32="mem.zone.zone_dma32";
const char* const c_zone_normal="mem.zone.zone_normal";
const char* const c_zone_highmem="mem.zone.zone_highmem";

const int COMMONTLB=0;
const int CLUSTERTLB=1;
struct Pair
{
	Pair( unsigned one , unsigned two) : first(one),second(two)
	{}
	unsigned first;
	unsigned second;
};

struct Triple
{
	Triple(unsigned one , unsigned two, unsigned three):
		first(one),second(two),third(three)
	{}
	unsigned first;
	unsigned second;
	unsigned third;
};

/*---------global functions and types----------*/
typedef std::list<unsigned> entry_list;
typedef std::list<unsigned>::iterator entry_list_ptr;

typedef std::list<Pair> pair_list;
typedef std::list<Pair>::iterator pair_list_ptr;

typedef std::list<Triple> triple_list;
typedef std::list<Triple>::iterator triple_list_ptr;

enum PagingStyle
{
	Legacy_Normal,	//4KB page
	Legacy_Huge,	//4MB page
	PAE_Normal,		//4KB page
	PAE_Huge,		//2MB page
	LongMode_Normal,	//4KB page
	LongMode_Middle,	//2MB page
	LongMode_Huge,		//1GB page
	Hash_Normal,
	Hash_Chain,
	Cuckoo_Normal,
	Cuckoo_Elastic
};

enum ZoneType
{
	Zone_DMA,	//0-16MB
	Zone_DMA32,	
	Zone_Normal,	//16-896MB
	Zone_HighMem,	//over 896MB
	MAX_NR_ZONES	//4
};

enum GFPMASK
{
   GFP_DMA=0x01,		//require page located in DMA zone
   GFP_HighMem=0x02,
   GFP_DMA32=0x04,
   GFP_COLD=0x08
};

enum AllocFlags
{
	ALLOC_NO_WATERMARKS=0x01,
	ALLOC_WMARK_MIN=0x02,
	ALLOC_WMARK_LOW=0x04,
	ALLOC_WMARK_HIGH=0x08,
	ALLOC_HEADER=0x10,
	ALLOC_HIGH=0x20,
	ALLOC_CPUSET=0x40
};
enum TlbFlag
{
	GLOBAL = 0x01,
	UNCACHEABLE = 0x02,
	PAT = 0x04,
	NOEXEC = 0x08,
	VALID = 0x10,
	SHARED = 0x20,
	DIRTY = 0x40
};
enum PAGE_FAULT
{
	DRAM_BUFFER_FAULT,
	DRAM_PAGE_FAULT,
	PCM_PAGE_FAULT
};

enum TLBSearchResult
{
	OverThres,
	unOverThres,
	InDRAM
};

enum EVICTSTYLE
{
	LRU = 0x01,
	HOTNESSAware = 0x02,
	HotMonitorTLBLRU= 0x03,
	CLUSTER= 0x04
};

enum DRAMEVICTSTYLE
{
	Equal = 0x01,
	Fairness = 0x02
};

enum ThresAdAction
{
	InitZero,
	Incre,
	Decre
};

enum PageTableEntryBits
{
	P=0x1,	//bit 0,present
	RW=0x2,	//bit 1, read/write
	PERMISSION=0x4,	//bit 2, user/kernel
	PWT=0x8,	//bit 3, page write through
	PCD=0x10,	//bit 4, page cache disable
	A=0x20,		//bit 5 , accessed
	D=0x40,		//bit 6,dirty, only exist in lowest page translation hierarchy
	PS=0x80,	//bit 7, page size , default is 0(4KB)
	G=0x100,	//bit 8,global,only exist in lowest page translation hierarchy
	SHAR=0x200,	//bit 9, private or shared, default is 0 (private)
	DC=0x400,   //bit 10, page cached in dram
};

enum MemType {
    MEM_SIMPLE = 0,
    MEM_MD1 = 1,
    MEM_WEAVEMD1 = 2,
    MEM_WEAVESIMPLE = 3,
    MEM_DDR = 4,
    MEM_DRAMSIM = 5,
    MEM_DETAILED = 6,
    MEM_RAMULATOR = 7
};

enum PUTaskSchedulerType{
	PU_SCHE_LOAD_BALANCE = 0,
	PU_SCHE_MAX
};

enum IdealMemNetWorkType{
	IDEAL_MEM_NETWORK_LOCALVAULT = 0,
	IDEAL_MEM_NETWORK_LOCALSTACK
};
#endif

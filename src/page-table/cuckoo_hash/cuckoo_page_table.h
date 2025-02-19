/*
 *  Buxin Tu
 * email:    tubuxin0621@gmail.com
 * function: extend hash-table in FSA-VM
 */

/*
 * Copyright (C) 2024 Buxin Tu (tubuxin0621@gmail.com)
 */

#ifndef _CUCKOO_PAGE_TABLE_H
#define _CUCKOO_PAGE_TABLE_H
#include "common/common_functions.h"
#include "common/global_const.h"
#include "blake2-impl.h"
#include "blake2.h"
#include "locks.h"
#include "page-table/baseline_hash/city.h"
#include "memory_hierarchy.h"
#include "page-table/comm_page_table_op.h"
#include "page-table/page_table_entry.h"
#include <iterator>
#include <map>
/*#----Hash-Paging(supports 4KB&&2MB&&1GB)---#*/
class CuckooPaging : public BasePaging {
  public:
    // CuckooPaging(PagingStyle selection);
    CuckooPaging(PagingStyle selection);
    ~CuckooPaging();
    virtual PagingStyle get_paging_style() { return mode; }
    virtual PageTable *get_root_directory() { return hptr[0]; }
    virtual Address access(MemReq &req);
    virtual Address access(MemReq &req, g_vector<MemObject *> &parents,
                           g_vector<uint32_t> &parentRTTs,
                           BaseCoreRecorder *cRec, bool sendPTW);
    virtual bool unmap_page_table(Address addr);
    int map_page_table(Address addr, Page *pg_ptr, BasePDTEntry *&mapped_entry);
    virtual int map_page_table(Address addr, Page *pg_ptr);
    virtual int map_page_table(uint32_t req_id, Address addr, Page *pg_ptr,
                               bool is_write);
    virtual bool allocate_page_table(Address addr, Address size);
    virtual void remove_root_directory();
    virtual bool remove_page_table(Address addr, Address size);
    virtual void calculate_stats(std::ofstream &vmof) {}
    virtual void calculate_stats() {}
    virtual void lock() { futex_lock(&table_lock); }
    virtual void unlock() { futex_unlock(&table_lock); }
    virtual void rehash(unsigned d);
    virtual void rehash_gradual(unsigned d);
  protected:
    uint64_t allocate_table_entry(uint64_t hash_id, uint64_t &pt_id, Page *pg_ptr, unsigned d);
    //allocate
    // remove

  private:
    uint64_t hash_function(Address address, unsigned d);
    uint64_t loadPageTables(MemReq &req, g_vector<uint64_t> &pgt_addrs,
                            g_vector<MemObject *> &parents,
                            g_vector<uint32_t> &parentRTTs, bool sendPTW);
    inline uint64_t getPGTAddr(uint64_t pageNo, uint32_t entry_id)
        __attribute__((always_inline));

  public:
    vector<PageTable *> hptr;
    vector<uint64_t> rehash_count;
    vector<uint64_t> cur_pte_num;
    uint64_t* keys;
    string hash_func;
  private:
    PagingStyle mode;
    lock_t table_lock;
    double scale;
    uint64_t ways;
    double threshold;
};

// class PagingFactory
// {
// 	public:
// 		PagingFactory()
// 		{}
// 		static BasePaging* CreatePaging( PagingStyle mode)
// 		{
// 			std::string mode_str = pagingmode_to_string(mode);
// 			debug_printf("paging mode is "+mode_str);
// 			if( mode_str == "Legacy" )
// 				return (new NormalPaging(mode));
// 			else if( mode_str == "PAE")
// 				return (new PAEPaging(mode));
// 			else if( mode_str == "LongMode")
// 				return (new CuckooPaging(mode));
// 			else
// 				return (new NormalPaging(Legacy_Normal));	//default
// is legacy paging
// 		}
// };
#endif

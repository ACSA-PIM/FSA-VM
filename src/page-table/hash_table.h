/*
 *  Buxin Tu
 * email:    tubuxin0621@gmail.com
 * function: extend hash-table in FSA-VM
 */

/*
 * Copyright (C) 2024 Buxin Tu (tubuxin0621@gmail.com)
 */

#ifndef _HASH_TABLE_H
#define _HASH_TABLE_H
#include "common/common_functions.h"
#include "common/global_const.h"
#include "locks.h"
#include "page-table/city.h"
#include "memory_hierarchy.h"
#include "page-table/comm_page_table_op.h"
#include "page-table/page_table_entry.h"
#include <iterator>
#include <map>
/*#----Hash-Paging(supports 4KB&&2MB&&1GB)---#*/
class HashPaging : public BasePaging {
  public:
    HashPaging(PagingStyle selection);
    ~HashPaging();
    virtual PagingStyle get_paging_style() { return mode; }
    virtual PageTable *get_root_directory() { return hptr; }
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
    virtual void calculate_stats(std::ofstream &vmof) {//Need to implement
        // long unsigned overhead =
        //     (long unsigned)(cur_pt_num + cur_pd_num + cur_pdp_num) * PAGE_SIZE;
        // vmof << "Error migrated pages:" << error_migrated_pages << std::endl;
        // vmof << "page directory pointer number:" << cur_pdp_num << std::endl;
        // vmof << "page directory number:" << cur_pd_num << std::endl;
        // vmof << "page table number:" << cur_pt_num << std::endl;
        // vmof << "overhead of page table storage:"
        //      << (double)overhead / (double)(1024 * 1024) << " MB" << std::endl;
    }
    virtual void calculate_stats() {//Need to implement
        // long unsigned overhead =
        //     (long unsigned)(cur_pt_num + cur_pd_num + cur_pdp_num) * PAGE_SIZE;
        // info("Error migrated pages:%d", error_migrated_pages);
        // info("page directory pointer number:%d", cur_pdp_num);
        // info("page directory number:%d", cur_pd_num);
        // info("page table number:%d", cur_pt_num);
        // info("overhead of page table storage:%f MB",
        //      (double)overhead / (double)(1024 * 1024));
    }
    virtual void lock() { futex_lock(&table_lock); }
    virtual void unlock() { futex_unlock(&table_lock); }

  protected:
  //TUBUXIN: hash table functions can be implemented here, including allocate, remove, and so on.
    uint64_t allocate_table_entry(uint64_t hash_id);
    //allocate
    // remove

  private:
    uint64_t hash_function(Address address);
    uint64_t loadPageTables(MemReq &req, g_vector<uint64_t> &pgt_addrs,
                            g_vector<MemObject *> &parents,
                            g_vector<uint32_t> &parentRTTs, bool sendPTW);
    inline uint64_t getPGTAddr(uint64_t pageNo, uint32_t entry_id)
        __attribute__((always_inline));

  public:
    PageTable *hptr;

  private:
    PagingStyle mode;
    uint64_t cur_pte_num;
    lock_t table_lock;
    uint64_t table_size;
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
// 				return (new HashPaging(mode));
// 			else
// 				return (new NormalPaging(Legacy_Normal));	//default
// is legacy paging
// 		}
// };
#endif

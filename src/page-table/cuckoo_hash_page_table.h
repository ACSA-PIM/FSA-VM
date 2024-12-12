#ifndef _CUCKOO_HASH_PAGE_TABLE_H
#define _CUCKOO_HASH_PAGE_TABLE_H

#include "common/common_functions.h"
#include "common/global_const.h"
#include "locks.h"
#include "page-table/cuckoo/city.h"
#include "memory_hierarchy.h"
#include "page-table/comm_page_table_op.h"
#include "page-table/page_table_entry.h"
// #include "./cuckoo/elastic_cuckoo_table.h"
#include "log.h"
#include "zsim.h"
#include <iterator>
#include <vector>
#include <map>

#define CUCKOO_WAYS 8
// #define CUCKOO_TABLE_SIZE 65536
#define CUCKOO_TABLE_SIZE 128

class CuckooHashPaging : public BasePaging {
public:
    CuckooHashPaging(PagingStyle selection);
    ~CuckooHashPaging();
    PagingStyle get_paging_style() { return mode; }
    PageTable *get_root_directory() { return nullptr; } // I didn't implement it.
    Address access(MemReq& req);
    Address access(MemReq& req , g_vector<MemObject*> &parents, g_vector<uint32_t> &parentRTTs, BaseCoreRecorder* cRec, bool sendPTW);
    bool unmap_page_table(Address addr);
    int map_page_table(Address addr, Page* pg_ptr);
    int map_page_table(uint32_t req_id, Address addr, Page* pg_ptr, bool is_write);
    bool allocate_page_table(Address addr , Address size);
    void remove_root_directory(); // Removes all tables. Used in the destructor.
    void lock() { futex_lock(&table_lock); };
    void unlock() { futex_unlock(&table_lock); };

    vector<PageTable*> cuckoo_hash_tables;
    // elasticCuckooTable_t elasticCuckooHT_4KB;

private:
    PagingStyle mode;
    uint64_t table_size; // Entries in each cuckoo hash table.
    lock_t table_lock;
};

#endif
#include "page-table/cuckoo_hash_page_table.h"

CuckooHashPaging::CuckooHashPaging(PagingStyle select)
    : mode(select) {
        info("[CUCKOO HASH START!!!!!!!!]");
    if (zinfo->buddy_allocator) {
        Page *page = zinfo->buddy_allocator->allocate_pages(0);
        if (page) {
            for (uint64_t i = 0; i < CUCKOO_WAYS; ++i) {
                PageTable *table = gm_memalign<PageTable>(CACHE_LINE_BYTES, 1);
                cuckoo_hash_tables.push_back(new (table) PageTable(CUCKOO_TABLE_SIZE, page));
            }            
            table_size = CUCKOO_TABLE_SIZE;
        } else {
            panic("Cannot allocate a page for page directory!");
        }
    } else {
        info("[I DIDN'T IMPLEMENT IT!!!!!!!!]");
    }
    futex_init(&table_lock);
}

CuckooHashPaging::~CuckooHashPaging() { remove_root_directory(); }

Address CuckooHashPaging::access(MemReq &req) {
    return 0;
}

Address CuckooHashPaging::access(MemReq& req , g_vector<MemObject*> &parents, g_vector<uint32_t> &parentRTTs, BaseCoreRecorder* cRec, bool sendPTW) {
    Address addr = req.lineAddr << lineBits;
    Address vpageno = get_bit_value<Address>(addr, 12, 47);
    // elem_t elem_4KB;
    // elem_4KB.valid = 1;
    // elem_4KB.value = vpageno;
    // std::vector<elem_t> accessedAddresses_4KB = find_elastic_ptw(&elem_4KB, &elasticCuckooHT_4KB);
    return 0;
}

bool CuckooHashPaging::unmap_page_table(Address addr) {
    info("[TEST!!!!!!!! unmap_page_table invoked]");
    return 0;
}

int CuckooHashPaging::map_page_table(Address addr, Page* pg_ptr) {
    info("[TEST!!!!!!!! map_page_table invoked]");
    return 0;
}

int CuckooHashPaging::map_page_table(uint32_t req_id, Address addr, Page* pg_ptr, bool is_write) {
    info("[TEST!!!!!!!! map_page_table invoked]");
    return 0;
}

bool CuckooHashPaging::allocate_page_table(Address addr , Address size) {
    info("[TEST!!!!!!!! allocate_page_table invoked]");
    return 0;
}

void CuckooHashPaging::remove_root_directory() {
    for (auto table : cuckoo_hash_tables) {
        if (table) {
            delete table;
        }
    }
    cuckoo_hash_tables.clear(); 
}
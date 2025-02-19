/*
 *  Buxin Tu
 * email:    tubuxin0621@gmail.com
 * function: extend cuckoo hash-table in FSA-VM
 */

/*
 * Copyright (C) 2025 Buxin Tu (tubuxin0621@gmail.com)
 */

#include "page-table/cuckoo_hash/cuckoo_table.h"
#include "common/common_functions.h"
#include "core.h"
#include "galloc.h"
#include "memory_hierarchy.h"
#include "mmu/memory_management.h"
#include "pad.h"
#include "page-table/page_table_entry.h"
#include "timing_event.h"
#include "page-table/baseline_hash/city.h"
#include "zsim.h"
#include <iterator>
#include <list>
#include <vector>
#include <unordered_map>
#include <immintrin.h>
/*-----------Cuckoo Paging--------------*/
#define hmask(n) (hsize(n) - 1)
#define hsize(n) (1 << (n))
#define MAX_ALLOCATE 32
CuckooPaging::CuckooPaging(PagingStyle select)
    : mode(select) {
    PageTable *table = gm_memalign<PageTable>(CACHE_LINE_BYTES, 1);
    assert(zinfo);
    hptr.resize(zinfo->cuckoo_d, NULL);
    rehash_count.resize(zinfo->cuckoo_d, 0);
    cur_pte_num.resize(zinfo->cuckoo_d, 0);
    keys = (uint64_t *)malloc(zinfo->cuckoo_d * sizeof(uint64_t));
    ways = zinfo->cuckoo_d;
    scale = zinfo->cuckoo_scale;
    hash_func = "blake2";
    if (zinfo->buddy_allocator) {
        for(int i=0; i<zinfo->cuckoo_d; i++) {
            Page *page = zinfo->buddy_allocator->allocate_pages(0);
            if (page) {
                hptr[i] = new (table) PageTable(zinfo->cuckoo_size, page);
                hptr[i]->map_count = zinfo->cuckoo_size;
                _rdrand64_step((unsigned long long*)&keys[i]);
            } else {
                panic("Cannot allocate a page for page directory!");
            }
        } 
    } else {
        hptr[0] = new (table) PageTable(zinfo->cuckoo_size);
    }
    futex_init(&table_lock);
}

CuckooPaging::~CuckooPaging() { remove_root_directory(); }

/*****-----functional interface of Hash-Paging----*****/
//cityhash for the hash function in hash table

uint64_t CuckooPaging::hash_function(Address address, unsigned d) {
    uint64_t result = 0;
    if(hash_func == "blake2") {
        blake2b(&result, 8, &address, 8, &keys[d], 8);
    }
    else if(hash_func == "city") {
        result = CityHash64WithSeed((const char *)&address, 8, keys[d]);
        result = result & hmask(64);
    }
    else {//default
        blake2b(&result, 8, &address, 8, &keys[d], 8);
    }
    return result;
}

//find idle entry in cuckoo tables
uint64_t CuckooPaging::allocate_table_entry(uint64_t vpageno, uint64_t &pt_id, Page *pg_ptr, unsigned d) {
    int walk_time = 0;
    while(d < ways) {
        uint64_t hash_id = hash_function(vpageno, d)%hptr[d]->map_count;
        // std::cout<<"Virtual page id: "<<vpageno<<" hash_id: "<<hash_id<<std::endl;
        BasePDTEntry *entry = (*hptr[d])[hash_id];
        // std::cout<<"virtual page id in the slot: "<<entry->get_vpn()<<std::endl;
        // std::cout<<"the entry is present: "<<entry->is_present()<<std::endl;
        if(!entry->is_present()) {
            validate_page(hptr[d], hash_id, pg_ptr);
            entry->set_vpn(vpageno);
            pt_id = hash_id;
            cur_pte_num[d]++;
            return d;
        }
        else {
            //lookup in the next d-th table
            //get the info of the entry in the d-th table
            Page *cuckoo_pg_ptr = entry->get_page();
            uint64_t cuckoo_vpn = entry->get_vpn();
            uint64_t cuckoo_id = -1;
            //recursion: allocate the entry in the d-th table to the (d+1)th table
            walk_time = allocate_table_entry(cuckoo_vpn, cuckoo_id, cuckoo_pg_ptr, d+1);
            validate_page(hptr[d], hash_id, pg_ptr);
            entry->set_vpn(vpageno);
            pt_id = hash_id;
            return walk_time;
        }
    }
    std::cout<<"out of table"<<std::endl;
    return -1;
}

int CuckooPaging::map_page_table(Address addr, Page *pg_ptr) {
    BasePDTEntry *entry;
    return map_page_table(addr, pg_ptr, entry);
}

int CuckooPaging::map_page_table(uint32_t req_id, Address addr, Page *pg_ptr,
                                   bool is_write) {
    BasePDTEntry *entry;
    int latency = map_page_table(addr, pg_ptr, entry);
    if (entry != NULL) {
        entry->set_lrequester(req_id);
        entry->set_accessed();
        if (is_write)
            entry->set_dirty();
    }
    return latency;
}

int CuckooPaging::map_page_table(Address addr, Page *pg_ptr,
                                   BasePDTEntry *&mapped_entry) {
    mapped_entry = NULL;
    int latency = 0;
    // std::cout<<"map:"<<std::hex<<addr<<std::endl;
    uint64_t vpageno = get_bits(addr, 12, 47);
    assert((vpageno != -1));
    uint64_t pt_id = -1;
    int d = allocate_table_entry(vpageno, pt_id, pg_ptr, 0);
    // std::cout<<"pt_id: "<<pt_id<<" "<<"size: "<<hptr[0]->map_count<<std::endl;
    assert(is_valid(hptr[0], pt_id) && d != -1);
    mapped_entry = (*hptr[0])[pt_id];
    std::cout << (double)(cur_pte_num[d])/(double)(hptr[d]->map_count) << std::endl;    
    if((double)(cur_pte_num[d])/(double)(hptr[d]->map_count) > zinfo->cuckoo_threshold) {
        // std::cout<< "rehashing" << std::endl;
        rehash(d);
        //rehash_gruadual(d);
    }
    latency = zinfo->mem_access_time * (1 + d);
    return latency;
}

bool CuckooPaging::unmap_page_table(Address addr) {
    //need to implement
    return true;
}

Address CuckooPaging::access(MemReq &req) {
    assert(0); //shouldn't come here
    return 0;
}

uint64_t CuckooPaging::loadPageTables(MemReq &req,
                                        g_vector<uint64_t> &pgt_addrs,
                                        g_vector<MemObject *> &parents,
                                        g_vector<uint32_t> &parentRTTs,
                                        bool sendPTW) {
    if (!sendPTW)
        return req.cycle;
    uint64_t startCycle = req.cycle;
    uint64_t respCycle = req.cycle;
    uint64_t lookupLat = 0;
    TimingRecord first_ptwTR;
    first_ptwTR.clear();
    // Link all reqs in a page table walk
    for (int i = 0; i < pgt_addrs.size(); i++) {
        uint64_t lineAddr = pgt_addrs[i] >> lineBits;
        uint32_t parentId = getParentId(lineAddr, parents.size());
        MESIState dummyState = MESIState::I;
        MemReq pgt_req = {lineAddr,    GETS,       req.childId,
                          &dummyState, startCycle, req.childLock,
                          dummyState,  req.srcId,  req.flags};
        pgt_req.threadId = req.threadId;
        pgt_req.isPIMInst = req.isPIMInst;
        if (i == 0) {
            pgt_req.isFirstPTW = true;
            if (pgt_addrs.size() > 1)
                pgt_req.isLastPTW = false;
        } else {
            pgt_req.isFirstPTW = false;
            pgt_req.isLastPTW = false;
        }
        if (i == (pgt_addrs.size() - 1)) {
            pgt_req.isLastPTW = true;
        }
        uint64_t nextLevelLat = parents[parentId]->access(pgt_req) - startCycle;
        uint32_t netLat = parentRTTs[parentId];
        uint64_t Lat = nextLevelLat + netLat;
        lookupLat = lookupLat < Lat? Lat : lookupLat;
        if (zinfo->eventRecorders[req.srcId]->hasRecord()) {
            // std::cout << i <<"hasRecord"<<std::endl;
            TimingRecord c_tr = zinfo->eventRecorders[req.srcId]->popRecord();
            if (!first_ptwTR.isValid() && c_tr.isValid()) {
                // std::cout << i <<" first_ptwTR is not valid & c_tr is valid"<<std::endl;
                assert(c_tr.endEvent);
                first_ptwTR = c_tr;
            } else {
                // std::cout << i <<"first_ptwTR is valid"<<std::endl;
                assert(c_tr.isPTW);
                // Exist a previous page table memory access
                first_ptwTR.endEvent->addChild(
                    c_tr.startEvent, zinfo->eventRecorders[req.srcId]);
                first_ptwTR.endEvent = c_tr.startEvent;
            }
        }
    }
    respCycle += lookupLat;
    // reinsert event
    if (first_ptwTR.isValid())
        zinfo->eventRecorders[req.srcId]->pushRecord(first_ptwTR);
    return respCycle;
}

inline uint64_t CuckooPaging::getPGTAddr(uint64_t pageNo, uint32_t entry_id) {
    uint64_t pgAddr = pageNo << zinfo->page_shift;
    pgAddr |= (ENTRY_SIZE_512 * entry_id);
    return pgAddr;
}

Address CuckooPaging::access(MemReq &req, g_vector<MemObject *> &parents,
                               g_vector<uint32_t> &parentRTTs,
                               BaseCoreRecorder *cRec, bool sendPTW) {
    g_vector<uint64_t> pgt_addrs;
    vector<uint64_t> hash_ids;
    Address addr = req.lineAddr << lineBits;
    std::cout<<get_bits(addr, 12, 47)<<std::hex<<std::endl;
    Address vpageno = get_bits(addr, 12, 47);
    void *ptr = NULL;
    for(int i = 0; i < ways; i++) {
        //get the hash ids in d-ary cuckoo hash table:
        hash_ids.push_back(hash_function(vpageno, i)%hptr[i]->map_count);
        // std::cout<<"hash_function works, the hash ids are pushed" << std::endl;
        //fetch PTE in these d tables, haven't implemented the Cuckoo walk table & cache now:
        BasePDTEntry *ht_ptr = (*(PageTable *)hptr[i])[hash_ids[i]];
        pgt_addrs.push_back(getPGTAddr(hptr[i]->get_page_no(), hash_ids[i]));
        // std::cout<<"vpn in hstable: "<<ht_ptr->get_vpn()<<" vpn accessed: "<<vpageno<<std::endl;
        if(ht_ptr->is_page_assigned() && ht_ptr->get_vpn() == vpageno) {
            // std::cout<<"VPN: " << vpageno << " in " << i << "-ary table." <<std::endl;
            // update page table flags
            std::cout<<"VPN: " << vpageno << " in " << i << "-ary table." <<std::endl;
            ht_ptr->set_lrequester(req.srcId, req.triggerPageShared);
            ht_ptr->set_accessed();
            if (req.type == PUTS) {
                if (!ht_ptr->is_dirty())
                    req.triggerPageDirty = true;
                ht_ptr->set_dirty();
            }
            req.pageDirty = ht_ptr->is_dirty();
            req.pageShared = ht_ptr->is_shared();
            ptr = get_next_level_address<void*>(hptr[i], hash_ids[i]);
            break;
        }
    }
    if (!ptr) { //PTE accessed don't get the page ptr
        std::cout << "No found the PPN, Now handle the access to d-ary cuckoo tables:" << std::endl;
        req.cycle =
            loadPageTables(req, pgt_addrs, parents, parentRTTs, sendPTW);
        return PAGE_FAULT_SIG;
    }
    // std::cout << "Get the PPN, Now handle the access to d-ary cuckoo tables:" << std::endl;
    std::cout << ((Page *)ptr)->pageNo << std::endl;
    req.cycle = loadPageTables(req, pgt_addrs, parents, parentRTTs, sendPTW);
    return ((Page *)ptr)->pageNo;
}

// allocate

bool CuckooPaging::allocate_page_table(Address addr, Address size) {
    //need to implement(however, hash related table is not suitable for this)
    return true;
}

// remove
void CuckooPaging::remove_root_directory() {
    if (!hptr.empty()) {
        for (int i = 0; i < hptr.size(); i++)
        {
            for(int j = 0; j < hptr[i]->map_count; j++) {
                invalidate_page(hptr[i], j);
            }
        }
        hptr.clear();
    }
}

bool CuckooPaging::remove_page_table(Address addr, Address size) {
    //need to implement(have not seen where to use it)
    return true;
}

// rehash: first, we implement a non-gruadual rehash codes.
// When the map count reaches the threshold, we rehash the table immediately.
// Allocate a new larger table to replace the old one and rehash all the entries in the old tables.
// Until this process finishes, the OS do nothing but wait.
void CuckooPaging::rehash(unsigned d) {
    rehash_count[d]++;
    PageTable *new_table = gm_memalign<PageTable>(CACHE_LINE_BYTES, 1);
    new_table = new PageTable(hptr[d]->map_count * scale, hptr[d]->get_page());
    _rdrand64_step((unsigned long long *)&keys[d]);
    for(int i = 0; i < hptr[d]->map_count; i++) {
        BasePDTEntry *entry = (*hptr[d])[i];
        if(entry->is_present()) {
            uint64_t vpageno = entry->get_vpn();
            // std::cout<<"vpn: "<<vpageno<<std::endl;
            Page *pg_ptr = entry->get_page();
            uint64_t hash_id = hash_function(vpageno, d)%new_table->map_count;
            validate_page(new_table, hash_id, pg_ptr);
            BasePDTEntry *new_entry = (*new_table)[hash_id];
            new_entry->set_vpn(vpageno);
            vpageno = new_entry->get_vpn();
            // std::cout<<"vpn: "<<vpageno<<std::endl;
            assert(is_valid(new_table, hash_id));
        }
    }
    // std::cout<<"rehashing finished"<<std::endl;
    // std::cout<<"delete old table"<<std::endl;
    PageTable *old_table = hptr[d];

    // delete old_table;
    // std::cout<<"succeed deletion"<<std::endl;
    hptr[d] = new_table;
    // std::cout<<"succeed replacement"<<std::endl;
}

void CuckooPaging::rehash_gradual(unsigned d) {
    rehash_count[d]++;
    PageTable *new_table = gm_memalign<PageTable>(CACHE_LINE_BYTES, 1);
    new_table = new PageTable(hptr[d]->map_count * scale, hptr[d]->get_page());
    _rdrand64_step((unsigned long long *)&keys[d]);
    for(int i = 0; i < hptr[d]->map_count; i++) {
        BasePDTEntry *entry = (*hptr[d])[i];
        if(entry->is_present()) {
            uint64_t vpageno = entry->get_vpn();
            // std::cout<<"vpn: "<<vpageno<<std::endl;
            Page *pg_ptr = entry->get_page();
            uint64_t hash_id = hash_function(vpageno, d)%new_table->map_count;
            validate_page(new_table, hash_id, pg_ptr);
            BasePDTEntry *new_entry = (*new_table)[hash_id];
            new_entry->set_vpn(vpageno);
            vpageno = new_entry->get_vpn();
            // std::cout<<"vpn: "<<vpageno<<std::endl;
            assert(is_valid(new_table, hash_id));
        }
    }
    // std::cout<<"rehashing finished"<<std::endl;
    // std::cout<<"delete old table"<<std::endl;
    PageTable *old_table = hptr[d];

    // delete old_table;
    // std::cout<<"succeed deletion"<<std::endl;
    hptr[d] = new_table;
    // std::cout<<"succeed replacement"<<std::endl;
}
// void CuckooPaging::rehash_gradual() {
//     //need to implement
// }


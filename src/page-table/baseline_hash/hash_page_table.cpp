/*
 *  Buxin Tu
 * email:    tubuxin0621@gmail.com
 * function: extend hash-table in FSA-VM
 */

/*
 * Copyright (C) 2024 Buxin Tu (tubuxin0621@gmail.com)
 */

#include "page-table/baseline_hash/hash_page_table.h"
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
/*-----------Hash Paging--------------*/

HashPaging::HashPaging(PagingStyle select)
    : mode(select) {
    PageTable *table = gm_memalign<PageTable>(CACHE_LINE_BYTES, 1);
    assert(zinfo);
    if (zinfo->buddy_allocator) {
        Page *page = zinfo->buddy_allocator->allocate_pages(0);
        if (page) {
            table_size = zinfo->hdc_size;
            hptr = new (table) PageTable(table_size, page);//page table size is DDDD PTEs
        } else {
            panic("Cannot allocate a page for page directory!");
        }
    } else {
        hptr = new (table) PageTable(zinfo->hdc_size);
    }
    futex_init(&table_lock);
    cur_pte_num = 0;
    threshold = zinfo->hdc_threshold;
    scale = zinfo->hdc_scale;
}

HashPaging::~HashPaging() { remove_root_directory(); }

/*****-----functional interface of Hash-Paging----*****/
//cityhash for the hash function in hash table
uint64_t HashPaging::hash_function(Address address) {
    uint64_t mask=0;
    for(uint64_t i=0 ;i<64 ;i++){
        mask+=(uint64_t)pow(2,i);
    }
    char*  new_address = (char*) address;
    uint64_t result=CityHash64((const char*)&address,8) & (mask);
    return result;
}

//find idle entry in hash table, using open addressing
uint64_t HashPaging::allocate_table_entry(uint64_t hash_id) {
    uint64_t i = 0;
    while(i < table_size){
        BasePDTEntry *entry = (*hptr)[hash_id];
        if(!entry->is_present()) return hash_id;
        hash_id=(hash_id+1)%table_size;
        i++;
    }
    return -1;// hash table is full. no idle entry left.
}

uint64_t HashPaging::reallocate_table_entry(uint64_t hash_id) {
    uint64_t i = 0;
    while(i < new_hptr->map_count){
        BasePDTEntry *entry = (*new_hptr)[hash_id];
        if(!entry->is_present()) return hash_id;
        hash_id=(hash_id+1)%new_hptr->map_count;
        i++;
    }
    return -1;// hash table is full. no idle entry left.
}

int HashPaging::map_page_table(Address addr, Page *pg_ptr) {
    BasePDTEntry *entry;
    return map_page_table(addr, pg_ptr, entry);
}

int HashPaging::map_page_table(uint32_t req_id, Address addr, Page *pg_ptr,
                                   bool is_write) {
    BasePDTEntry *entry;
    int latency = map_page_table(addr, pg_ptr, entry);
    if (entry != NULL) {
        cur_pte_num++;
        // cout <<"allocated pte num: "<<cur_pte_num<<endl;
        entry->set_lrequester(req_id);
        entry->set_accessed();
        entry->set_vpn(get_bits(addr, 12, 47));
        if (is_write)
            entry->set_dirty();
    }
    return latency;
}
           
int HashPaging::map_page_table(Address addr, Page *pg_ptr,
                                   BasePDTEntry *&mapped_entry) {
    mapped_entry = NULL;
    int latency = 0;
    // std::cout<<"map:"<<std::hex<<addr<<std::endl;
    uint64_t vpageno =  get_bits(addr, 12, 47);
    assert((vpageno != (unsigned)(-1)));
    int alloc_time = 0;
    uint64_t hash_id = hash_function(vpageno)%table_size;
    uint64_t pt_id = allocate_table_entry(hash_id);
    assert(pt_id != (unsigned)(-1));
    validate_page(hptr, pt_id, pg_ptr);
    assert(is_valid(hptr, pt_id));
    mapped_entry = (*hptr)[pt_id];
    // std::cout<<"footprint rate: "<<(double)(cur_pte_num)/(double)(hptr->map_count)<<std::endl;
    if((double)(cur_pte_num)/(double)(hptr->map_count) > threshold) {
        std::cout<< "rehashing" << std::endl;
        rehash();
        //rehash_gruadual(d);
    }
    latency = zinfo->mem_access_time * (1 + alloc_time);
    return latency;
}

bool HashPaging::unmap_page_table(Address addr) {
    //need to implement
    return true;
}

Address HashPaging::access(MemReq &req) {
    assert(0); //shouldn't come here
    return 0;
}

uint64_t HashPaging::loadPageTables(MemReq &req,
                                        g_vector<uint64_t> &pgt_addrs,
                                        g_vector<MemObject *> &parents,
                                        g_vector<uint32_t> &parentRTTs,
                                        bool sendPTW) {
    if (!sendPTW)
        return req.cycle;

    uint64_t startCycle = req.cycle;
    uint64_t respCycle = req.cycle;
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
        respCycle += nextLevelLat + netLat;
        startCycle = respCycle;

        if (zinfo->eventRecorders[req.srcId]->hasRecord()) {
            TimingRecord c_tr = zinfo->eventRecorders[req.srcId]->popRecord();
            if (!first_ptwTR.isValid() && c_tr.isValid()) {
                assert(c_tr.endEvent);
                first_ptwTR = c_tr;
            } else {
                assert(c_tr.isPTW);
                // Exist a previous page table memory access
                first_ptwTR.endEvent->addChild(
                    c_tr.startEvent, zinfo->eventRecorders[req.srcId]);
                first_ptwTR.endEvent = c_tr.startEvent;
            }
        }
    }
    // reinsert event
    if (first_ptwTR.isValid())
        zinfo->eventRecorders[req.srcId]->pushRecord(first_ptwTR);
    return respCycle;
}

inline uint64_t HashPaging::getPGTAddr(uint64_t pageNo, uint32_t entry_id) {
    uint64_t pgAddr = pageNo << zinfo->page_shift;
    pgAddr |= (ENTRY_SIZE_512 * entry_id);
    return pgAddr;
}

Address HashPaging::access(MemReq &req, g_vector<MemObject *> &parents,
                               g_vector<uint32_t> &parentRTTs,
                               BaseCoreRecorder *cRec, bool sendPTW) {
    g_vector<uint64_t> pgt_addrs;
    Address addr = req.lineAddr << lineBits;
    Address vpageno = get_bits(addr, 12, 47);
    // std::cout <<"accessing vpn: "<<vpageno<<std::endl;
    uint64_t hash_id = hash_function(vpageno) % table_size;
    pgt_addrs.push_back(getPGTAddr(hptr->get_page_no(), hash_id%512));
    BasePDTEntry *ht_ptr = (*(PageTable *)hptr)[hash_id];
    //@BUXIN: this is open addressing codes, from 204 to 214
    void *ptr = NULL;
    while(ht_ptr->is_page_assigned() && hash_id < table_size) {
        if(ht_ptr->get_vpn() == vpageno) {
            ptr = get_next_level_address<void>(hptr, hash_id);
            break;
        }
            
        hash_id++;
        ht_ptr = (*(PageTable *)hptr)[hash_id];
        pgt_addrs.push_back(getPGTAddr(hptr->get_page_no(), hash_id%512));
    }
    ptr = get_next_level_address<void>(hptr, hash_id);
    if (!ptr) { //PTE accessed don't get the page ptr
        req.cycle =
            loadPageTables(req, pgt_addrs, parents, parentRTTs, sendPTW);
        return PAGE_FAULT_SIG;
    }
    // update page table flags
    ht_ptr->set_lrequester(req.srcId, req.triggerPageShared);
    ht_ptr->set_accessed();
    if (req.type == PUTS) {
        if (!ht_ptr->is_dirty())
            req.triggerPageDirty = true;
        ht_ptr->set_dirty();
    }
    req.pageDirty = ht_ptr->is_dirty();
    req.pageShared = ht_ptr->is_shared();
    req.cycle = loadPageTables(req, pgt_addrs, parents, parentRTTs, sendPTW);
    return ((Page *)ptr)->pageNo;
}

void HashPaging::rehash() {
    rehash_count++;
    PageTable *new_table = gm_memalign<PageTable>(CACHE_LINE_BYTES, 1);
    new_hptr = new (new_table) PageTable((uint64_t)(hptr->map_count * scale), hptr->get_page());
    for(int i = 0; i < hptr->map_count; i++) {
        BasePDTEntry *entry = (*hptr)[i];
        if(entry->is_present()) {
            uint64_t vpageno = entry->get_vpn();
            // std::cout<<"vpn: "<<vpageno<<std::endl;
            Page *pg_ptr = entry->get_page();
            uint64_t hash_id = hash_function(vpageno)%new_hptr->map_count;
            hash_id = reallocate_table_entry(hash_id);//@buxin: get the idle entry in new hash table
            BasePDTEntry *new_entry = (*new_hptr)[hash_id];
            new_entry->set_vpn(vpageno);
            vpageno = new_entry->get_vpn();
            validate_page(new_hptr, hash_id, pg_ptr);
            // std::cout<<"vpn: "<<vpageno<<std::endl;
            assert(is_valid(new_hptr, hash_id));
        }
    }
    table_size = new_hptr->map_count;
    hptr = new_hptr;
    new_hptr = NULL;
}

// allocate

bool HashPaging::allocate_page_table(Address addr, Address size) {
    //need to implement(however, hash table is not suitable for this)
    return true;
}

// remove
void HashPaging::remove_root_directory() {
    if (hptr) {
        for (unsigned i = 0; i < 67108864; i++) {
            if (is_present(hptr, i)) {
                invalidate_page(hptr, i);
            }
        }
        delete hptr;
        hptr = NULL;
    }
}

bool HashPaging::remove_page_table(Address addr, Address size) {
    //need to implement(have not seen where to use it)
    return true;
}




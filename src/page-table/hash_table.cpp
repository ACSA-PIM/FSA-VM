/*
 *  Buxin Tu
 * email:    tubuxin0621@gmail.com
 * function: extend hash-table in FSA-VM
 */

/*
 * Copyright (C) 2024 Buxin Tu (tubuxin0621@gmail.com)
 */

#include "page-table/hash_table.h"
#include "common/common_functions.h"
#include "core.h"
#include "galloc.h"
#include "memory_hierarchy.h"
#include "mmu/memory_management.h"
#include "pad.h"
#include "page_table_entry.h"
#include "timing_event.h"
#include "page-table/city.h"
#include "zsim.h"
#include <iterator>
#include <list>
#include <vector>

/*-----------Hash Paging--------------*/
// PageTable* HashPaging::pml4;
HashPaging::HashPaging(PagingStyle select)
    : mode(select), cur_pdp_num(0), cur_pd_num(0), cur_pt_num(0) {
    PageTable *table = gm_memalign<PageTable>(CACHE_LINE_BYTES, 1);
    assert(zinfo);
    if (zinfo->buddy_allocator) {
        Page *page = zinfo->buddy_allocator->allocate_pages(0);
        if (page) {
            pml4 = new (table) PageTable(4096, page);
            table_size = 4096;
        } else {
            panic("Cannot allocate a page for page directory!");
        }
    } else {
        pml4 = new (table) PageTable(4096);
    }
    futex_init(&table_lock);
    error_migrated_pages = 0;
}

HashPaging::~HashPaging() { remove_root_directory(); }

/*****-----functional interface of LongMode-Paging----*****/
uint64_t HashPaging::hash_function(Address address){
    uint64_t mask=0;
    for(uint64_t i=0 ;i<64 ;i++){
        mask+=(uint64_t)pow(2,i);
    }
    char*  new_address = (char*) address;
    uint64_t result=CityHash64((const char*)&address,8) & (mask);
    return result;
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
        entry->set_lrequester(req_id);
        entry->set_accessed();
        entry->set_vpn(addr);
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
    uint64_t vpageno =  get_bit_value<uint64_t>(addr, 12, 47);
    assert((vpageno != (unsigned)(-1)));
    int alloc_time = 0;
    uint hash_id = hash_function(vpageno)%table_size;
    validate_page(pml4, hash_id, pg_ptr);
    assert(is_valid(pml4, hash_id));
    mapped_entry = (*pml4)[hash_id];
    latency = zinfo->mem_access_time * (1 + alloc_time);
    return latency;
}

bool HashPaging::unmap_page_table(Address addr) {
    //need to implement
    return true;
}

Address HashPaging::access(MemReq &req) {
    Address addr = req.lineAddr;
    unsigned pml4_id, pdp_id, pd_id, pt_id;
    get_domains(addr, pml4_id, pdp_id, pd_id, pt_id, mode);
    // point to page table pointer table
    PageTable *pdp_ptr = get_next_level_address<PageTable>(pml4, pml4_id);
    if (!pdp_ptr) {
        return PAGE_FAULT_SIG;
    }
    if (mode == LongMode_Huge) {
        req.cycle += (zinfo->mem_access_time * 2);
    }
    // point to page or page directory
    void *ptr = get_next_level_address<void>(pdp_ptr, pdp_id);
    if (!ptr) {
        return PAGE_FAULT_SIG;
    }
    if (mode == LongMode_Middle) {
        assert(pd_id != (unsigned)(-1));
        // point to page
        ptr = get_next_level_address<void>((PageTable *)ptr, pd_id);
        req.cycle += (zinfo->mem_access_time * 3);
        if (!ptr) {
            return PAGE_FAULT_SIG;
        }
    } else if (mode == LongMode_Normal) {
        assert(pd_id != (unsigned)(-1));
        assert(pt_id != (unsigned)(-1));
        // point to page table
        ptr = get_next_level_address<void>((PageTable *)ptr, pd_id);
        req.cycle += (zinfo->mem_access_time * 4);
        if (!ptr) {
            return PAGE_FAULT_SIG;
        }
        // point to page
        ptr = get_next_level_address<void>((PageTable *)ptr, pt_id);
        if (!ptr) {
            return PAGE_FAULT_SIG;
        }
    }
    /* bool write_back = false;
    uint32_t access_counter = 0;
    if( req.type==WRITEBACK)
    {
            access_counter = req.childId;
            write_back = true;
    }
    return get_block_id(req,ptr, write_back ,access_counter); */
    return ((Page *)ptr)->pageNo;
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
    Address vpageno = get_bit_value<Address>(addr, 12, 47);
    uint64_t hash_id = hash_function(vpageno) % table_size;
    // TUBUXIN: only once push, in ideal hash cases.
    pgt_addrs.push_back(getPGTAddr(pml4->get_page_no(), hash_id%512));
    BasePDTEntry *ht_ptr = (*(PageTable *)pml4)[hash_id];
    //TUBUXIN: this is open addressing codes, todo: modify the page fault codes to use it.
    // void *ptr = NULL;
    // while(ht_ptr->is_page_assigned() && hash_id < table_size) {
    //     if(ht_ptr->get_vpn() == vpageno) {
    //         ptr = get_next_level_address<void>(pml4, hash_id);
    //         break;
    //     }
            
    //     hash_id++;
    //     ht_ptr = (*(PageTable *)pml4)[hash_id];
    //     pgt_addrs.push_back(getPGTAddr(pml4->get_page_no(), hash_id%512));
    // }
    void *ptr = get_next_level_address<void>(pml4, hash_id);
    if (!ptr) { //TUBUXIN: PTE that accessed don't get the page ptr
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
    if (req.triggerPageShared && ht_ptr->remapped_times)
        error_migrated_pages++;
    req.pageDirty = ht_ptr->is_dirty();
    req.pageShared = ht_ptr->is_shared();
    req.cycle = loadPageTables(req, pgt_addrs, parents, parentRTTs, sendPTW);
    return ((Page *)ptr)->pageNo;
}

// allocate

bool HashPaging::allocate_page_table(Address addr, Address size) {
    return true;
}

// remove
void HashPaging::remove_root_directory() {
    if (pml4) {
        for (unsigned i = 0; i < ENTRY_512; i++) {
            if (is_present(pml4, i)) {
                //need to implement
            }
        }
        delete pml4;
        pml4 = NULL;
    }
}

bool HashPaging::remove_page_table(Address addr, Address size) {
    return true;
}




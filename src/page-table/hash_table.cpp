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

/*-----------LongMode Paging--------------*/
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

inline PageTable *
HashPaging::get_tables(unsigned level,
                           std::vector<unsigned> entry_id_list) {
    assert(level >= 1);
    PageTable *table;
    unsigned i = 0;
    table = get_next_level_address<PageTable>(pml4, entry_id_list[i]);
    level--;
    i++;
    while (level > 0) {
        table = get_next_level_address<PageTable>(table, entry_id_list[i]);
        if (!table)
            return NULL;
        if (table)
            level--;
        i++;
    }
    return table;
}

bool HashPaging::unmap_page_table(Address addr) {
    unsigned pml4_id, pdp_id, pd_id, pt_id;
    std::vector<unsigned> entry_id_vec(4);

    get_domains(addr, pml4_id, pdp_id, pd_id, pt_id, mode);
    entry_id_vec[0] = pml4_id;
    entry_id_vec[1] = pdp_id;
    entry_id_vec[2] = pd_id;
    PageTable *table = NULL;
    // point to page directory pointer table
    if (mode == LongMode_Normal) {
        table = get_tables(3, entry_id_vec);
        if (!table) {
            debug_printf("didn't find entry indexed with %ld !", addr);
            return false;
        }
        invalidate_page(table, pt_id);
    } else if (mode == LongMode_Middle) {
        table = get_tables(2, entry_id_vec);
        if (!table) {
            debug_printf("didn't find entry indexed with %ld !", addr);
            return false;
        }
        invalidate_page(table, pd_id);
    } else if (mode == LongMode_Huge) {
        table = get_tables(1, entry_id_vec);
        if (!table) {
            debug_printf("didn't find entry indexed with %ld !", addr);
            return false;
        }
        invalidate_page(table, pdp_id);
    }
    return false;
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

uint64_t HashPaging::loadPageTable(MemReq &req, uint64_t startCycle,
                                       uint64_t pageNo, uint32_t entry_id,
                                       g_vector<MemObject *> &parents,
                                       g_vector<uint32_t> &parentRTTs,
                                       BaseCoreRecorder *cRec, bool sendPTW) {
    if (!sendPTW)
        return startCycle;
    uint64_t respCycle = startCycle;
    uint64_t pgAddr = pageNo << zinfo->page_shift;
    pgAddr |= (ENTRY_SIZE_512 * entry_id);
    uint64_t lineAddr = pgAddr >> lineBits;
    uint32_t parentId = getParentId(lineAddr, parents.size());
    MESIState dummyState = MESIState::I;
    // memmory req for page table
    MemReq pgt_req = {lineAddr,    GETS,       req.childId,
                      &dummyState, startCycle, req.childLock,
                      dummyState,  req.srcId,  req.flags};
    uint64_t nextLevelLat = parents[parentId]->access(pgt_req) - startCycle;
    uint32_t netLat = parentRTTs[parentId];
    respCycle += nextLevelLat + netLat;
    cRec->record(startCycle, startCycle, respCycle);
    return respCycle;
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
PageTable *
HashPaging::allocate_page_directory_pointer(unsigned pml4_entry_id,
                                                int &allocate_time) {
    // allocate_time = 0;
    assert(pml4_entry_id < 512 && cur_pdp_num < ENTRY_512);
    if (!is_present(pml4, pml4_entry_id)) {
        PageTable *table_tmp = gm_memalign<PageTable>(CACHE_LINE_BYTES, 1);
        PageTable *table = NULL;
        if (zinfo->buddy_allocator) {
            Page *page = zinfo->buddy_allocator->allocate_pages(0);
            if (page)
                table = new (table_tmp) PageTable(ENTRY_512, page);
            else
                panic("Cannot allocate a page for page directory!");
        } else {
            table = new (table_tmp) PageTable(ENTRY_512);
        }
        validate_entry(pml4, pml4_entry_id, table);
        allocate_time++;
        cur_pdp_num++;
        return table;
    }
    PageTable *pg_dir_p =
        get_next_level_address<PageTable>(pml4, pml4_entry_id);
    return pg_dir_p;
}

bool HashPaging::allocate_page_directory_pointer(entry_list pml4_entry) {
    bool succeed = true;
    int allocate_time;
    for (entry_list_ptr it = pml4_entry.begin(); it != pml4_entry.end(); it++) {
        if (allocate_page_directory_pointer(*it, allocate_time) == NULL) {
            succeed = false;
            debug_printf("allocate page directory pointer for entry %d of pml4 "
                         "table failed",
                         *it);
        }
    }
    return succeed;
}

bool HashPaging::allocate_page_directory(pair_list high_level_entry) {
    bool succeed = true;
    int allocate_time;
    for (pair_list_ptr it = high_level_entry.begin();
         it != high_level_entry.end(); it++) {
        if (allocate_page_directory((*it).first, (*it).second, allocate_time) ==
            NULL) {
            debug_printf("allocate (pml4_entry_id , page directory pointer "
                         "entry id)---(%d,%d) failed",
                         (*it).first, (*it).second);
            succeed = false;
        }
    }
    return succeed;
}

PageTable *HashPaging::allocate_page_directory(unsigned pml4_entry_id,
                                                   unsigned pdpt_entry_id,
                                                   int &allocate_time) {
    PageTable *pdp_table =
        get_next_level_address<PageTable>(pml4, pml4_entry_id);
    if (pdp_table) {
        if (!is_present(pdp_table, pdpt_entry_id)) {
            PageTable *table_tmp = gm_memalign<PageTable>(CACHE_LINE_BYTES, 1);
            PageTable *pd_table = NULL;
            if (zinfo->buddy_allocator) {
                Page *page = zinfo->buddy_allocator->allocate_pages(0);
                if (page)
                    pd_table = new (table_tmp) PageTable(ENTRY_512, page);
                else
                    panic("Cannot allocate a page for page directory!");
            } else {
                pd_table = new (table_tmp) PageTable(ENTRY_512);
            }
            validate_entry(pdp_table, pdpt_entry_id, pd_table);
            cur_pd_num++;
            allocate_time++;
            return pd_table;
        } else {
            PageTable *table =
                get_next_level_address<PageTable>(pdp_table, pdpt_entry_id);
            return table;
        }
    } else {
        if (allocate_page_directory_pointer(pml4_entry_id, allocate_time)) {
            PageTable *pdpt_table =
                get_next_level_address<PageTable>(pml4, pml4_entry_id);
            PageTable *table_tmp = gm_memalign<PageTable>(CACHE_LINE_BYTES, 1);
            PageTable *pd_table = NULL;
            if (zinfo->buddy_allocator) {
                Page *page = zinfo->buddy_allocator->allocate_pages(0);
                if (page)
                    pd_table = new (table_tmp) PageTable(ENTRY_512, page);
                else
                    panic("Cannot allocate a page for page directory!");
            } else {
                pd_table = new (table_tmp) PageTable(ENTRY_512);
            }
            validate_entry(pdpt_table, pdpt_entry_id, pd_table);
            allocate_time++;
            cur_pd_num++;
            return pd_table;
        }
    }
    return NULL;
}

bool HashPaging::allocate_page_table(triple_list high_level_entry) {
    bool succeed = true;
    int time;
    for (triple_list_ptr it = high_level_entry.begin();
         it != high_level_entry.end(); it++) {
        if (!allocate_page_table((*it).first, (*it).second, (*it).third, time))
            succeed = false;
    }
    return succeed;
}

PageTable *HashPaging::allocate_page_table(unsigned pml4_entry_id,
                                               unsigned pdpt_entry_id,
                                               unsigned pdt_entry_id,
                                               int &alloc_time) {
    alloc_time = 0;
    assert(mode == LongMode_Normal);
    PageTable *pdp_table =
        get_next_level_address<PageTable>(pml4, pml4_entry_id);
    if (pdp_table) {
        PageTable *pd_table =
            get_next_level_address<PageTable>(pdp_table, pdpt_entry_id);
        if (pd_table) {
            if (is_present(pd_table, pdt_entry_id)) {
                PageTable *table =
                    get_next_level_address<PageTable>(pd_table, pdt_entry_id);
                return table;
            } else {
                PageTable *table_tmp =
                    gm_memalign<PageTable>(CACHE_LINE_BYTES, 1);
                PageTable *table = NULL;
                if (zinfo->buddy_allocator) {
                    Page *page = zinfo->buddy_allocator->allocate_pages(0);
                    if (page)
                        table = new (table_tmp) PageTable(ENTRY_512, page);
                    else
                        panic("Cannot allocate a page for page table!");
                } else {
                    table = new (table_tmp) PageTable(ENTRY_512);
                }
                validate_entry(pd_table, pdt_entry_id, table);
                cur_pt_num++;
                alloc_time++;
                return table;
            }
        }
        // page_direcory doesn't exist allocate
        else {
            if (allocate_page_directory(pml4_entry_id, pdpt_entry_id,
                                        alloc_time)) {
                // get page directory
                PageTable *page_dir =
                    get_next_level_address<PageTable>(pdp_table, pdpt_entry_id);
                PageTable *table = gm_memalign<PageTable>(CACHE_LINE_BYTES, 1);
                PageTable *pg_table = NULL;
                if (zinfo->buddy_allocator) {
                    Page *page = zinfo->buddy_allocator->allocate_pages(0);
                    if (page)
                        pg_table = new (table) PageTable(ENTRY_512, page);
                    else
                        panic("Cannot allocate a page for page table!");
                } else {
                    pg_table = new (table) PageTable(ENTRY_512);
                }
                validate_entry(page_dir, pdt_entry_id, pg_table);
                cur_pt_num++;
                alloc_time++;
                return pg_table;
            }
        }
    } else {
        PageTable *g_tables = gm_memalign<PageTable>(CACHE_LINE_BYTES, 3);
        PageTable *pdp_table = NULL;
        PageTable *pd_table = NULL;
        PageTable *pg_table = NULL;
        if (zinfo->buddy_allocator) {
            Page *page1 = zinfo->buddy_allocator->allocate_pages(0);
            if (page1) {
                pdp_table = new (&g_tables[0]) PageTable(ENTRY_512, page1);
                Page *page2 = zinfo->buddy_allocator->allocate_pages(0);
                if (page2) {
                    pd_table = new (&g_tables[1]) PageTable(ENTRY_512, page2);
                    Page *page3 = zinfo->buddy_allocator->allocate_pages(0);
                    if (page3) {
                        pg_table =
                            new (&g_tables[2]) PageTable(ENTRY_512, page3);
                    } else {
                        panic("Cannot allocate a page for page table!");
                    }
                } else {
                    panic("Cannot allocate a page for page table!");
                }
            } else {
                panic("Cannot allocate a page for page table!");
            }
        } else {
            pdp_table = new (&g_tables[0]) PageTable(ENTRY_512);
            pd_table = new (&g_tables[1]) PageTable(ENTRY_512);
            pg_table = new (&g_tables[2]) PageTable(ENTRY_512);
        }
        validate_entry(pml4, pml4_entry_id, pdp_table);
        cur_pdp_num++;
        validate_entry(pdp_table, pdpt_entry_id, pd_table);
        cur_pd_num++;
        validate_entry(pd_table, pdt_entry_id, pg_table);
        cur_pt_num++;
        alloc_time += 3;
        return pg_table;
    }
    return NULL;
}

bool HashPaging::allocate_page_table(Address addr, Address size) {
    bool succeed = true;
    assert(mode == LongMode_Normal);
    if (addr & 0x1fffff) {
        fatal("must align with 2MB");
        succeed = false;
    }
    unsigned pml4_entry = get_pml4_off(addr, mode);
    unsigned pdp_entry = get_page_directory_pointer_off(addr, mode);
    unsigned pd_entry = get_page_directory_off(addr, mode);
    unsigned page_table_num = (size + 0x1fffff) >> 21;
    int time;
    for (unsigned i = 0; i < page_table_num; i++) {
        if (allocate_page_table(pml4_entry, pdp_entry, pd_entry + i, time) ==
            NULL)
            succeed = false;
    }
    return succeed;
}

// remove
void HashPaging::remove_root_directory() {
    if (pml4) {
        for (unsigned i = 0; i < ENTRY_512; i++) {
            if (is_present(pml4, i)) {
                remove_page_directory_pointer(i);
            }
        }
        delete pml4;
        pml4 = NULL;
    }
}

bool HashPaging::remove_page_directory_pointer(unsigned pml4_entry_id) {
    bool succeed = true;
    PageTable *pdp_table =
        get_next_level_address<PageTable>(pml4, pml4_entry_id);
    if (pdp_table) {
        if (mode != LongMode_Huge) {
            for (unsigned i = 0; i < ENTRY_512; i++) {
                if (is_present(pdp_table, i)) {
                    if (remove_page_directory(pml4_entry_id, i) == false)
                        succeed = false;
                }
            }
        }
        invalidate_entry<PageTable>(pml4, pml4_entry_id);
        cur_pdp_num--;
    }
    return succeed;
}

bool HashPaging::remove_page_directory_pointer(entry_list pml4_entry) {
    bool succeed = true;
    for (entry_list_ptr it = pml4_entry.begin(); it != pml4_entry.end(); it++) {
        if (remove_page_directory_pointer(*it))
            succeed = false;
    }
    return succeed;
}

bool HashPaging::remove_page_directory(unsigned pml4_entry_id,
                                           unsigned pdp_entry_id) {
    assert(mode != LongMode_Huge && pml4_entry_id < 512 && pdp_entry_id < 512);
    PageTable *pdp_table = NULL;
    if ((pdp_table = get_next_level_address<PageTable>(pml4, pml4_entry_id))) {
        PageTable *pd_table = NULL;
        if ((pd_table =
                 get_next_level_address<PageTable>(pdp_table, pdp_entry_id))) {
            if (mode == LongMode_Normal) {
                for (unsigned i = 0; i < ENTRY_512; i++)
                    if (is_present(pd_table, i))
                        remove_page_table(pml4_entry_id, pdp_entry_id, i);
            }
            invalidate_entry<PageTable>(pdp_table, pdp_entry_id);
            cur_pd_num--;
        }
    }
    return true;
}

bool HashPaging::remove_page_directory(pair_list high_level_entry) {
    for (pair_list_ptr it = high_level_entry.begin();
         it != high_level_entry.end(); it++) {
        remove_page_directory((*it).first, (*it).second);
    }
    return true;
}

bool HashPaging::remove_page_table(unsigned pml4_entry_id,
                                       unsigned pdp_entry_id,
                                       unsigned pd_entry_id) {
    assert(mode == LongMode_Normal);
    PageTable *pdp_table = NULL;
    if ((pdp_table = get_next_level_address<PageTable>(pml4, pml4_entry_id))) {
        PageTable *pd_table = NULL;
        if ((pd_table =
                 get_next_level_address<PageTable>(pdp_table, pdp_entry_id))) {
            PageTable *pg_table = NULL;
            if ((pg_table = get_next_level_address<PageTable>(pd_table,
                                                              pd_entry_id))) {
                if (mode == LongMode_Normal) {
                    for (unsigned i = 0; i < ENTRY_512;
                         i++) // also reclaim the pages pointed by entries of
                              // the page table
                        invalidate_page(pg_table, i);
                }
                invalidate_entry<PageTable>(pd_table, pd_entry_id);
                cur_pt_num--;
            }
        }
    }
    return true;
}

bool HashPaging::remove_page_table(Address addr, Address size) {
    assert(mode == LongMode_Normal);
    if (addr & 0x1fffff) {
        debug_printf("address must align with 2MB");
        return false;
    }
    unsigned pml4_entry, pdp_entry, pd_entry, pt_entry;
    get_domains(addr, pml4_entry, pdp_entry, pd_entry, pt_entry, mode);
    unsigned page_table_num = (size + 0x1fffff) >> 21;
    for (unsigned i = 0; i < page_table_num; i++)
        remove_page_table(pml4_entry, pdp_entry, pd_entry + i);
    return true;
}

bool HashPaging::remove_page_table(triple_list high_level_entry) {
    for (triple_list_ptr it = high_level_entry.begin();
         it != high_level_entry.end(); it++)
        remove_page_table((*it).first, (*it).second, (*it).third);
    return true;
}


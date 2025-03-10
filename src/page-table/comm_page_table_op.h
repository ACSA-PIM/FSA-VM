
/*
 * 2015-xxx by YuJie Chen
 * email:    YuJieChen_hust@163.com
 * function: extend zsim-nvmain with some other simulation,such as tlb,page
 * table,page table,etc.
 */

/*
 * Copyright (C) 2020 Chao Yu (yuchaocs@gmail.com)
 */

#ifndef COMMON_PAGE_TABLE_OP_H
#define COMMON_PAGE_TABLE_OP_H
#include "common/global_const.h"
#include "memory_hierarchy.h"
#include "mmu/page.h"
#include "page-table/page_table_entry.h"
inline bool is_present(PageTable *table, unsigned entry_id) {
    return table->is_present(entry_id);
}

template <class T>
T *get_next_level_address(PageTable *table, unsigned entry_id) {
    return (T *)((*table)[entry_id])->get_next_level_address();
}

template <class T>
inline void set_next_level_address(PageTable *&table, unsigned entry_id,
                                   T *next_level_ptr) {
    ((*table)[entry_id])->set_next_level_address((void *)next_level_ptr);
}

inline void validate_page(PageTable *table, unsigned entry_id,
                          Page *next_level_addr) {
    // std::cout<<std::hex<<(*table)[entry_id]<<std::endl;
    assert(next_level_addr != NULL);
    (*table)[entry_id]->validate_page(next_level_addr);
}

inline void validate_entry(PageTable *table, unsigned entry_id,
                           PageTable *next_level_addr) {
    // std::cout<<std::hex<<(*table)[entry_id]<<std::endl;
    assert(next_level_addr->page != NULL);
    (*table)[entry_id]->validate_page_table(next_level_addr);
}

/* template<class T>
inline void validate_entry(PageTable* table , unsigned entry_id , T*
next_level_addr)
{
        //std::cout<<std::hex<<(*table)[entry_id]<<std::endl;
        (*table)[entry_id]->validate((void*)next_level_addr);
} */

inline bool is_valid(PageTable *table, unsigned entry_id) {
    return (*table)[entry_id]->is_present();
}

inline void invalidate_page(PageTable *table, unsigned entry_id) {
    if (is_present(table, entry_id))
        (*table)[entry_id]->invalidate_page();
}

template <class T>
inline void invalidate_entry(PageTable *table, unsigned entry_id) {
    ((*table)[entry_id])->invalidate_page_table<T>();
}

inline unsigned get_pagetable_off(Address addr, PagingStyle mode) {
    switch (mode) {
    case Legacy_Normal:
        return get_bit_value<unsigned>(addr, 12, 21);
    case PAE_Normal:
        return get_bit_value<unsigned>(addr, 21, 20);
    case LongMode_Normal:
        return get_bit_value<unsigned>(addr, 12, 20);
    default:
        return (unsigned)(-1);
    }
}

inline unsigned get_page_directory_off(Address addr, PagingStyle mode) {
    unsigned index;
    if (mode == Legacy_Normal || mode == Legacy_Huge)
        index = get_bit_value<unsigned>(addr, 22, 31);
    else if (mode == PAE_Normal || mode == PAE_Huge ||
             mode == LongMode_Normal || mode == LongMode_Middle)
        index = get_bit_value<unsigned>(addr, 21, 29);
    else
        index = (unsigned)(-1);
    return index;
}

inline unsigned get_page_directory_pointer_off(Address addr, PagingStyle mode) {
    if (mode == PAE_Normal || mode == PAE_Huge)
        return get_bit_value<unsigned>(addr, 30, 31);
    else if (mode == LongMode_Normal || mode == LongMode_Middle ||
             mode == LongMode_Huge)
        return get_bit_value<unsigned>(addr, 30, 38);
    else
        return (unsigned)(-1);
}

inline unsigned get_pml4_off(Address addr, PagingStyle mode) {
    if (mode == LongMode_Normal || mode == LongMode_Middle ||
        mode == LongMode_Huge)
        return get_bit_value<unsigned>(addr, 39, 47);
    else
        return (unsigned)(-1);
}

inline void get_domains(Address addr, unsigned &pml4, unsigned &pdp,
                        unsigned &pd, unsigned &pt, PagingStyle mode) {
    pml4 = (unsigned)(-1);
    pdp = (unsigned)(-1);
    pd = (unsigned)(-1);
    pt = (unsigned)(-1);
    switch (mode) {
    case Legacy_Normal:
        pd = get_bit_value<unsigned>(addr, 22, 31);
        pt = get_bit_value<unsigned>(addr, 12, 21);
        break;
    case Legacy_Huge:
        pd = get_bit_value<unsigned>(addr, 22, 31);
        break;
    case PAE_Normal:
        pt = get_bit_value<unsigned>(addr, 12, 20);
        pd = get_bit_value<unsigned>(addr, 21, 29);
        pdp = get_bit_value<unsigned>(addr, 30, 31);
        break;
    case PAE_Huge:
        pd = get_bit_value<unsigned>(addr, 21, 29);
        pdp = get_bit_value<unsigned>(addr, 30, 31);
        break;
    case LongMode_Normal:
        pt = get_bit_value<unsigned>(addr, 12, 20);
        pd = get_bit_value<unsigned>(addr, 21, 29);
        pdp = get_bit_value<unsigned>(addr, 30, 38);
        pml4 = get_bit_value<unsigned>(addr, 39, 47);
        break;
    case LongMode_Middle:
        pd = get_bit_value<unsigned>(addr, 21, 29);
        pdp = get_bit_value<unsigned>(addr, 30, 38);
        pml4 = get_bit_value<unsigned>(addr, 39, 47);
        break;
    case LongMode_Huge:
        pdp = get_bit_value<unsigned>(addr, 30, 38);
        pml4 = get_bit_value<unsigned>(addr, 39, 47);
        break;
    }
}

/* inline Address get_block_id(MemReq& req, void* pblock, bool write_back,
uint32_t  access_counter)
{
        if(!pblock)
                return INVALID_PAGE_ADDR;

        if( write_back)
        {
                //overlap field
                ((Page*)pblock)->set_overlap(access_counter );
        }
        req.childId = ((Page*)pblock)->get_overlap();
        //std::cout<<"ppn2:"<<(((Page*)pblock)->pageNo)<<std::endl;
        return ((Page*)pblock)->pageNo;
} */

inline uint32_t getParentId(Address lineAddr, uint32_t parentsSize) {
    // Hash things a bit
    uint32_t res = 0;
    uint64_t tmp = lineAddr;
    for (uint32_t i = 0; i < 4; i++) {
        res ^= (uint32_t)(((uint64_t)0xffff) & tmp);
        tmp = tmp >> 16;
    }
    return (res % parentsSize);
}
#endif

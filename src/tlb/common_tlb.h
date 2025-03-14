/*
 * 2015-xxx by YuJie Chen
 * email:    YuJieChen_hust@163.com
 * function: extend zsim-nvmain with some other simulation,such as tlb,page
 * table,page table,etc.
 */
/*
 * Copyright (C) 2020 Chao Yu (yuchaocs@gmail.com)
 */
#ifndef COMMON_TLB_H_
#define COMMON_TLB_H_
#include <set>
#include <unordered_map>

#include "common/common_functions.h"
#include "common/common_structures.h"
#include "common/global_const.h"
#include "common/trie.h"
#include "g_std/g_list.h"
#include "g_std/g_string.h"
#include "g_std/g_unordered_map.h"
#include "locks.h"
#include "memory_hierarchy.h"

template <class T> class CommonTlb : public BaseTlb {
  public:
    CommonTlb(const g_string &name, bool enable_timing_mode, unsigned tlb_size,
              unsigned hit_lat, unsigned res_lat, unsigned line_shift,
              unsigned page_shift, EVICTSTYLE policy = LRU)
        : tlb_entry_num(tlb_size), hit_latency(hit_lat),
          response_latency(res_lat), tlb_access_time(0), tlb_hit_time(0),
          tlb_evict_time(0), tlb_name_(name),
          enable_timing_mode(enable_timing_mode), line_shift(line_shift),
          page_shift(page_shift), page_size(1 << page_shift),
          evict_policy(policy) {
        assert(tlb_size > 0);
        tlb = gm_memalign<T *>(CACHE_LINE_BYTES, tlb_size);
        tlb_trie.clear();
        tlb_trie_pa.clear();
        for (unsigned i = 0; i < tlb_size; i++) {
            tlb[i] = new T;
            free_entry_list.push_back(tlb[i]);
        }
        insert_num = 0;
        futex_init(&tlb_lock);
    }

    ~CommonTlb() {
        tlb_trie.clear();
        tlb_trie_pa.clear();
    }
    /*-------------drive simulation related---------*/
    uint64_t access(MemReq &req) {
        // debug_printf("now comes to %d level tlb access\n", tlb_level);
        tlb_access_time++;
        Address virt_addr = req.lineAddr << line_shift;
        Address offset = virt_addr & (page_size - 1);
        Address vpn = virt_addr >> page_shift;
        tlb_address[vpn]++;
        T *entry = look_up(vpn);
        Address ppn;
        // TLB miss
        if (!entry) {
            debug_printf("tlb miss: vaddr:%llx , cycle: %d ", virt_addr,
                         req.cycle);
            if(tlb_level == 1) {
                debug_printf("miss incurs in l1 tlb, now access l2 tlb\n");
                ppn = next_level_tlb->access(req);
            } else if(tlb_level == 2) {
                req.srcId = srcId;
                req.flags = reqFlags;
                // debug_printf("miss incurs in l2 tlb, now start PTW\n");
                ppn = page_table_walker->access(req);
            }
            // update TLB
            T new_entry(vpn, ppn);
            insert_num++;
            new_entry.set_valid();
            if (req.pageShared)
                new_entry.set_page_shared();
            if (req.pageDirty)
                new_entry.set_page_dirty();
            // std::cout << "insert vpn: " << vpn << " ppn: " << ppn << std::endl;
            insert(vpn, new_entry);
        } else // TLB hit
        {
            debug_printf("tlb hit: vaddr:%llx , cycle: %d ", virt_addr,
                         req.cycle);
            if (enable_timing_mode)
                req.cycle += hit_latency;
            tlb_hit_time++;
            ppn = entry->p_page_no;
            req.pageShared = entry->is_page_shared();
            req.pageDirty = entry->is_page_dirty();
        }
        if (enable_timing_mode)
            req.cycle += response_latency;
        Address plineaddr = ((ppn << page_shift) | offset) >> line_shift;
        debug_printf("tlb access finish in l%d tlb, ppn: %llx, plineaddr: %llx\n", tlb_level, ppn, plineaddr);
        if(tlb_level == 1) return plineaddr;
        else if(tlb_level == 2) return ppn;
    }

    uint32_t shootdown(Address vpn) {
        T *entry = NULL;
        entry = look_up(vpn);
        if (entry) {
            entry->set_invalid();
            tlb_trie.erase(vpn);
            tlb_trie_pa.erase(entry->p_page_no);
            free_entry_list.push_back(entry);
        }
        uint32_t shootdown_lat = 0;
        if (enable_timing_mode)
            shootdown_lat = hit_latency;
        return shootdown_lat;
    }

    uint32_t update_tlb_flags(Address ppn, bool shared, bool dirty) {
        T *entry = NULL;
        entry = look_up_pa(ppn);
        if (entry) {
            if (shared)
                entry->set_page_shared();
            if (dirty)
                entry->set_page_dirty();
        }
        uint32_t set_lat = 0;
        if (enable_timing_mode)
            set_lat = hit_latency;
        return set_lat;
    }

    uint32_t update_ppn(Address ppn, Address new_ppn) {
        T *entry = NULL;
        entry = look_up_pa(ppn);
        if (entry) {
            entry->update_ppn(new_ppn);
        }
        uint32_t update_lat = 0;
        if (enable_timing_mode)
            if (entry)
                update_lat = 2 * hit_latency;
            else
                update_lat = hit_latency;
        return update_lat;
    }
    uint32_t update_entry(Address vpn, Address ppn) {
        T *entry = NULL;
        entry = look_up(vpn);
        if (entry) {
            entry->update_ppn(ppn);
        }
        uint32_t update_lat = 0;
        if (enable_timing_mode)
            if (entry)
                update_lat = 2 * hit_latency;
            else
                update_lat = hit_latency;
        return update_lat;
    }

    const char *getName() { return tlb_name_.c_str(); }
    /*-------------TLB hierarchy related------------*/
    void set_parent(BasePageTableWalker *pg_table_walker) {
        page_table_walker = pg_table_walker;
    }
    void set_next_level_tlb(BaseTlb *tlb) { next_level_tlb = tlb;}
    BaseTlb *get_next_level_tlb() { return next_level_tlb; }
    
    uint64_t get_access_time() { return tlb_access_time; }
    int get_level() { return tlb_level; }
    BasePageTableWalker *get_page_table_walker() { return page_table_walker; }

    /*-------------TLB operation related------------*/
    // TLB look up
    /*
     *@function: look up TLB entry from tlb according to virtual page NO. and
     *update lru_sequence then
     *@param vpage_no: vpage_no of entry searched;
     *@param update_lru: default is true; when tlb hit/miss,whether update
     *lru_sequence or not
     *@return: poniter of found TLB entry; NULL represents that TLB miss
     */
    T *look_up(Address vpage_no, bool update_lru = true) {
        // debug_printf("look up tlb vpage_no: %llx",vpage_no);
        // TLB hit,update lru_seq of tlb entry to the newest lru_seq
        T *result_node = NULL;
        futex_lock(&tlb_lock);
        if (tlb_trie.count(vpage_no)) {
            result_node = tlb_trie[vpage_no];
        }
        if (result_node && result_node->is_valid() && update_lru) {
            result_node->lru_seq = ++lru_seq;
        }
        futex_unlock(&tlb_lock);
        return result_node;
    }

    T *look_up_pa(Address ppn) {
        T *result_node = NULL;
        futex_lock(&tlb_lock);
        if (tlb_trie_pa.count(ppn)) {
            result_node = tlb_trie_pa[ppn];
        }
        futex_unlock(&tlb_lock);
        return result_node;
    }

    T *insert(Address vpage_no, T &entry) {
        futex_lock(&tlb_lock);
        // whether entry is already exists
        debug_printf("insert tlb of vpage no %llx in %s", vpage_no, tlb_name_.c_str());
        assert(!tlb_trie.count(vpage_no));
        // no free TLB entry
        if (free_entry_list.empty()) {
            tlb_evict_time++;
            evict(); // default is LRU
        }
        if (free_entry_list.empty() == false) {
            T *new_entry = free_entry_list.front();
            free_entry_list.pop_front();
            *new_entry = entry;
            new_entry->set_valid();
            new_entry->lru_seq = ++lru_seq;
            tlb_trie[vpage_no] = new_entry;
            tlb_trie_pa[new_entry->p_page_no] = new_entry;
            // std::cout<<"insert "<<new_entry->p_page_no<<" to pa"<<std::endl;
            futex_unlock(&tlb_lock);
            return new_entry;
        }
        futex_unlock(&tlb_lock);
        return NULL;
    }

    bool is_full() {
        if (free_entry_list->is_empty())
            return true;
        return false;
    }

    // flush all entry of TLB out
    bool flush_all() {
        free_entry_list.clear();
        futex_lock(&tlb_lock);
        tlb_trie_pa.clear();
        tlb_trie.clear();
        // get cleared tlb entry to free_entry_list
        for (unsigned i = 0; i < tlb_entry_num; i++) {
            tlb[i]->set_invalid();
            free_entry_list.push_back(tlb[i]);
        }
        futex_unlock(&tlb_lock);
        return true;
    }

    // default is LRU
    virtual T *evict() {
        T *tlb_entry = NULL;
        if (evict_policy == LRU) {
            unsigned entry_num = lru_evict();
            tlb_entry = tlb[entry_num];
        }
        // evict entry in position of min_lru_entry
        // remove handle from tlb_trie
        assert(tlb_trie.count(tlb_entry->v_page_no));
        tlb_trie.erase(tlb_entry->v_page_no);
        tlb_trie_pa.erase(tlb_entry->p_page_no);
        // std::cout<<"tlb evict, vpn:"<<tlb_entry->v_page_no<<"
        // ppn:"<<tlb_entry->p_page_no<<std::endl;
        tlb_entry->set_invalid();
        free_entry_list.push_back(tlb_entry);
        return tlb_entry; // return physical address recorded in this tlb entry
    }

    // evict entry according to lru sequence
    unsigned lru_evict() {
        assert(free_entry_list.empty());
        uint64_t min_lru_entry = 0;
        // find the smallest lru_seq by accessing all TLB Entries' lru_seq
        for (unsigned i = 1; i < tlb_entry_num; i++) {
            if ((tlb[i]->is_valid()) &&
                (tlb[i]->lru_seq < tlb[min_lru_entry]->lru_seq))
                min_lru_entry = i;
        }
        return min_lru_entry; // return physical address recorded in this tlb
                              // entry
    }

    bool delete_entry(Address vpage_no) {
        bool deleted = false;
        // look up tlb entry first
        T *delete_entry = look_up(vpage_no);
        if (delete_entry) {
            tlb_trie.erase(delete_entry->v_page_no);
            tlb_trie_pa.erase(delete_entry->p_page_no);
            delete_entry->set_invalid();
            free_entry_list.push_back(delete_entry);
            deleted = true;
        } else
            warning("there has no entry whose virtual page num is %ld", tlb);
        return deleted;
    }

    void flush_all_noglobal() {
        for (unsigned i = 0; i < tlb_entry_num; i++) {
            if (!tlb[i]->global) {
                if (tlb_trie.count(tlb[i]->v_page_no))
                    tlb_trie.erase(tlb[i]->v_page_no);
                if (tlb_trie_pa.count(tlb[i]->p_page_no))
                    tlb_trie_pa.erase(tlb[i]->p_page_no);
            }
        }
    }

    uint64_t calculate_stats(std::ofstream &vmof) {
        double tlb_hit_rate = (double)tlb_hit_time / (double)tlb_access_time;
        double tlb_miss_rate = (double)insert_num / (double)tlb_access_time;
        // info("%s access time:%lu \t hit time: %lu \t miss time: %lu \t evict
        // time:%lu \t hit rate:%.3f",tlb_name_.c_str() , tlb_access_time,
        // tlb_hit_time, (tlb_access_time - tlb_hit_time),tlb_evict_time,
        // tlb_hit_rate);
        vmof << tlb_name_ << " access time:" << tlb_access_time
             << "\t hit time:" << tlb_hit_time
             << "\t miss time:" << insert_num
             << "\t evict time:" << tlb_evict_time
             << "\t hit rate:" << tlb_hit_rate << std::endl;
        return tlb_access_time;
    }

    void address_stats(std::ofstream &addrof) {
        for (const auto& entry : tlb_address) {
            addrof << "TLB Virtual Page: " << std::hex << entry.first << ", Count: " << std::dec <<entry.second << std::endl;
        }
        addrof << "Total Virtual Page num: " << tlb_access_time << std::endl;
    }

    void clear_counter() {
        futex_lock(&tlb_lock);
        for (unsigned i = 0; i < tlb_entry_num; i++) {
            tlb[i]->clear_counter();
        }
        futex_unlock(&tlb_lock);
    }
    void setSourceId(uint32_t id) { srcId = id; }
    void setFlags(uint32_t flags) { reqFlags = flags; }
    void setLevel(int level) { tlb_level = level; }
    // lock_t tlb_access_lock;
    // lock_t tlb_lookup_lock;
    // static lock_t pa_insert_lock;
    bool enable_timing_mode;
    // number of tlb entries
    unsigned tlb_entry_num;
    unsigned hit_latency;
    unsigned response_latency;
    unsigned insert_num;
    // statistic data
    uint64_t tlb_access_time;
    uint64_t tlb_hit_time;
    uint64_t tlb_evict_time;
    std::unordered_map<Address, uint64_t> tlb_address;
    T **tlb;
    g_list<T *> free_entry_list;

    int lru_seq;
    // tlb trie tree
    g_unordered_map<Address, T *> tlb_trie;
    g_unordered_map<Address, T *> tlb_trie_pa;

    g_string tlb_name_;
    int tlb_level;
    // page table walker
    BasePageTableWalker *page_table_walker;
    BaseTlb *next_level_tlb;
    // eviction policy
    EVICTSTYLE evict_policy;
    lock_t tlb_lock;

    uint64_t page_size;
    uint64_t page_shift;
    unsigned line_shift;

    uint32_t srcId; // should match the core
    uint32_t reqFlags;
};
#endif

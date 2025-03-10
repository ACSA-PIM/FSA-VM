/*
 * 2015-xxx by YuJie Chen
 * email:    YuJieChen_hust@163.com
 * function: extend zsim-nvmain with some other simulation,such as tlb,page
 * table,page table,etc.
 */
/*
 * Copyright (C) 2020 Chao Yu (yuchaocs@gmail.com)
 */
#define CLUSTER_TLB_H_
#ifndef CLUSTER_TLB_H_
#define CLUSTER_TLB_H_
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

template <class T> class ClusterTlb : public BaseTlb {
  public:
    ClusterTlb(const g_string &name, bool enable_timing_mode, unsigned tlb_size,      
        unsigned hit_lat, unsigned res_lat, unsigned line_shift,
        unsigned page_shift, uint16_t cluster_size, EVICTSTYLE policy = CLUSTER)
        : tlb_entry_num(tlb_size), hit_latency(hit_lat),
          response_latency(res_lat), tlb_access_time(0), tlb_hit_time(0),
          tlb_evict_time(0), tlb_name_(name),
          enable_timing_mode(enable_timing_mode), line_shift(line_shift),
          page_shift(page_shift), page_size(1 << page_shift),
          evict_policy(policy), cluster_size(cluster_size) {
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

    ~ClusterTlb() {
        tlb_trie.clear();
        tlb_trie_pa.clear();
    }
    /*-------------drive simulation related---------*/
    uint64_t access(MemReq &req) {
        tlb_access_time++;
        Address virt_addr = req.lineAddr << line_shift;
        Address offset = virt_addr & (page_size - 1);
        Address vpn = virt_addr >> page_shift;
        Address basic_vpn = vpn >> cluster_size;
        tlb_address[basic_vpn]++;
        T *entry = look_up(basic_vpn);//@buxin: look up the vpn in cluster TLB
        Address ppn;
        // Cluster TLB Entry miss, look up in the regular TLB
        if (!entry) {
            debug_printf("cluster tlb miss: vaddr:%llx , cycle: %d ", virt_addr,
                         req.cycle);
            //@buxin: cluster TLB miss, now look up in the regular TLB:
            ppn = regular_tlb->access(req);
            //@buxin: initialize a cluster TLB Entry
            ClusterTlbEntry new_entry(vpn, ppn);
            insert_num++;
            new_entry.set_valid();
            if (req.pageShared)
                new_entry.set_page_shared();
            if (req.pageDirty)
                new_entry.set_page_dirty();
            // std::cout << "insert vpn: " << vpn << " ppn: " << ppn << std::endl;
            insert(vpn, ppn);
        } else // CLUSTER entry hit, now look up in the cluster entry
        {
            debug_printf("cluster tlb hit: vaddr:%llx , cycle: %d, look up in the entry", virt_addr,
                         req.cycle);
            if (enable_timing_mode)
                req.cycle += hit_latency;
            tlb_hit_time++;
            Address ppn = -1;
            ppn = entry->sub_look_up(vpn);
            if(ppn != -1) ppn = (entry->basic_p_page_no << cluster_size) | ppn;
            else {
                //@buxin: Cluster entry hit, but the entry is not in the cluster entry
                // now look up in the regular TLB
                ppn = regular_tlb->access(req);
                insert(vpn, ppn); //@buxin: update cluster TLB Entry
            }
        }
        if (enable_timing_mode)
            req.cycle += response_latency;
        Address plineaddr = ((ppn << page_shift) | offset) >> line_shift;
        return plineaddr;
    }

    uint32_t shootdown(Address vpn) {
        T *entry = NULL;
        entry = look_up(vpn);
        if (entry) {
            entry->set_invalid();
            tlb_trie.erase(vpn);
            tlb_trie_pa.erase(entry->basic_p_page_no);
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
    uint64_t get_access_time() { return tlb_access_time; }
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
    T *look_up(Address base_vpage_no) {
        // debug_printf("look up tlb vpage_no: %llx",vpage_no);
        // TLB hit,update lru_seq of tlb entry to the newest lru_seq
        T *result_node = NULL;
        futex_lock(&tlb_lock);
        if (tlb_trie.count(base_vpage_no)) {
            result_node = tlb_trie[base_vpage_no];
        }
        if (result_node && result_node->is_valid()) {
            result_node->lru_seq = ++lru_seq;
            result_node->update_priority();
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

    T *insert(Address vpage_no, Address ppage_no) {
        futex_lock(&tlb_lock);
        // insert a new entry in Cluster TLB
        Address base_vpn = get_bits(vpage_no, cluster_size, 64);
        Address base_ppn = get_bits(ppage_no, cluster_size, 64);
        T entry(base_vpn, base_ppn);
        debug_printf("insert tlb of vpage no %llx", vpage_no);
        //@buxin: here are 3 situation: 
        // 1. There's no base mapping in Cluster TLB: the entry can be clustered.
        // 2. current base mapping is aligned to the entry: the entry can be clustered.
        // 3. current base mapping is not aligned to the entry: the entry cannot be clustered, inserted into regular L2 TLB.
        if(!tlb_trie.count(base_vpn)) {//@buxin: situation 1: no base mapping in Cluster TLB
            // no free TLB entry
            if (free_entry_list.empty()) {
                tlb_evict_time++;
                evict(); // default is CLUSTER(LRU+USABLE, refer the paper)
            }
            if (free_entry_list.empty() == false) {
                T *new_entry = free_entry_list.front();
                free_entry_list.pop_front();
                *new_entry = entry;
                new_entry->set_valid();
                new_entry->lru_seq = ++lru_seq;
                new_entry->upadte_priority();
                tlb_trie[base_vpn] = new_entry;
                tlb_trie_pa[base_ppn] = new_entry;
                new_entry->update_priority();
                // std::cout<<"insert "<<new_entry->p_page_no<<" to pa"<<std::endl;
                futex_unlock(&tlb_lock);
                return new_entry;
            }
        }
        else if(tlb_trie[base_vpn]->basic_p_page_no == base_ppn) {//@buxin: situation 2: base mapping is aligned to the entry
            T *cur_entry = tlb_trie[base_vpn];
            cur_entry->insert(vpage_no, ppage_no);
            cur_entry->updatge_priority();
            futex_unlock(&tlb_lock);
            return cur_entry;
        }
        else {//@buxin: situation 3: base mapping is not aligned to the entry
            regular_tlb->insert();
            futex_unlock(&tlb_lock);
            return NULL;
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

    // Cluster TLB uses the policy proposed in the paper.
    // use update_priority() to update the priority of the entry
    // the algorithm：priority = a * lru_seq + b * usefulness
    virtual T *evict() {
        T *tlb_entry = NULL;
        if (evict_policy == CLUSTER) {
            unsigned entry_num = cluster_evict();
            tlb_entry = tlb[entry_num];
        }
        // evict entry in position of min_priority_entry
        // remove handle from tlb_trie
        assert(tlb_trie.count(tlb_entry->basic_v_page_no));
        tlb_trie.erase(tlb_entry->basic_v_page_no);
        tlb_trie_pa.erase(tlb_entry->basic_p_page_no);
        // std::cout<<"tlb evict, vpn:"<<tlb_entry->v_page_no<<"
        // ppn:"<<tlb_entry->p_page_no<<std::endl;
        tlb_entry->set_invalid();
        free_entry_list.push_back(tlb_entry);
        return tlb_entry; // return physical address recorded in this tlb entry
    }

    // evict entry according to priority sequence
    unsigned cluster_evict() {
        assert(free_entry_list.empty());
        uint64_t min_priority_entry = 0;
        // find the smallest lru_seq by accessing all TLB Entries' lru_seq
        for (unsigned i = 1; i < tlb_entry_num; i++) {
            if ((tlb[i]->is_valid()) && (tlb[i]->priority < tlb[min_priority_entry]->priority))
                min_priority_entry = i;
        }
        return min_priority_entry; // return physical address recorded in this tlb
                                // entry
    }

    bool delete_entry(Address basic_vpage_no) {
        bool deleted = false;
        // look up tlb entry first
        T *delete_entry = look_up(basic_vpage_no);
        if (delete_entry) {
            tlb_trie.erase(delete_entry->basic_v_page_no);
            tlb_trie_pa.erase(delete_entry->basic_p_page_no);
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
                if (tlb_trie.count(tlb[i]->basic_v_page_no))
                    tlb_trie.erase(tlb[i]->basic_v_page_no);
                if (tlb_trie_pa.count(tlb[i]->basic_p_page_no))
                    tlb_trie_pa.erase(tlb[i]->basic_p_page_no);
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
    // page table walker
    BasePageTableWalker *page_table_walker;
    // eviction policy
    EVICTSTYLE evict_policy;
    lock_t tlb_lock;

    uint64_t page_size;
    uint64_t page_shift;
    unsigned line_shift;
    uint16_t cluster_size;
    uint32_t srcId; // should match the core
    uint32_t reqFlags;
};
#endif



#ifndef PW_CACHE_H_
#define PW_CACHE_H_

#include "memory_hierarchy.h"
#include "g_std/g_string.h"
#include "g_std/g_vector.h"
#include "ramulator_mem_ctrl.h"
#include "stats.h"
#include <unordered_map>
#include <unordered_set>
/* Extends Cache with an L0 direct-mapped cache, optimized to hell for hits
 *
 * L1 lookups are dominated by several kinds of overhead (grab the cache locks,
 * several virtual functions for the replacement policy, etc.). This
 * specialization of Cache solves these issues by having a filter array that
 * holds the most recently used line in each set. Accesses check the filter array,
 * and then go through the normal access path. Because there is one line per set,
 * it is fine to do this without grabbing a lock.
 */
class pwc_Array : public GlobAlloc {// default set-associative cache array
    protected:
    uint32_t numLines;
    uint32_t numSets;
    uint32_t assoc;
    uint32_t setMask;
    unordered_map<uint32_t, unordered_set<Address>> lineMap;
    vector<list<Address>> lru_order;
    public:
    pwc_Array(uint32_t _numLines, uint32_t _assoc);
    virtual bool lookup(const Address lineAddr);
    virtual void insert(const Address lineAddr);
    virtual void evict(uint32_t set_id);
    private:
    virtual void LRUupdate(uint32_t set_id, Address lineAddr);
    virtual uint32_t get_set_id(Address lineAddr);

    virtual ~pwc_Array();
};

inline void pwc_Array::LRUupdate(uint32_t set_id, Address lineAddr) {
    lru_order[set_id].remove(lineAddr);
    lru_order[set_id].push_front(lineAddr);
}

inline uint32_t pwc_Array::get_set_id(Address lineAddr) {
    return lineAddr & setMask;
}

class pw_cache : public MemObject {
    public:
        pwc_Array* array;
        uint32_t numLines;
        uint32_t access_count;
        uint32_t miss_count;
    protected:
        uint32_t accLat;
        uint32_t invLat;
        g_string name;

    public:
        pw_cache(uint32_t _numLines,  uint32_t assoc, uint32_t _accLat, uint32_t _invLat, const g_string& _name);
        virtual uint64_t access(MemReq& req);
};
#endif
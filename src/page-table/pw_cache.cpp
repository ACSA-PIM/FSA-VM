#include "memory_hierarchy.h"
#include "g_std/g_vector.h"
#include "ramulator_mem_ctrl.h"
#include "stats.h"
#include <unordered_map>
#include <unordered_set>
#include "pw_cache.h"

//pwc_Array codes

pwc_Array::pwc_Array(uint32_t _numLines, uint32_t _assoc) 
    : numLines(_numLines), assoc(_assoc) {
    numSets = numLines / assoc;
    setMask = numSets - 1;
    lru_order.resize(numSets);
}

pwc_Array::~pwc_Array() {
    lineMap.clear();
}

bool pwc_Array::lookup(Address lineAddr) {
    uint32_t set_id = get_set_id(lineAddr);
    auto it = lineMap[set_id].find(lineAddr);
    if(it != lineMap[set_id].end()) {
        LRUupdate(set_id, lineAddr);
        return true;
    }
    return false;
}

void pwc_Array::evict(uint32_t set_id) {
    Address lineAddr = lru_order[set_id].back();
    lru_order[set_id].pop_back();
    lineMap[set_id].erase(lineAddr);
}

void pwc_Array::insert(Address lineAddr) {
    uint32_t set_id = get_set_id(lineAddr);
    if(lineMap[set_id].size() == assoc) {
        evict(set_id);
    }
    lineMap[set_id].insert(lineAddr);
    LRUupdate(set_id, lineAddr);
}

// pw_cache codes

pw_cache::pw_cache(uint32_t _numLines, uint32_t assoc, uint32_t _accLat, uint32_t _invLat, const string& _name) 
    : numLines(_numLines), accLat(_accLat), invLat(_invLat), name(_name), access_count(0), miss_count(0) {
        array = new pwc_Array(numLines, assoc);
    }
uint64_t pw_cache::access(MemReq &req) {
    req.pwc_hit = false;
    if(array->lookup(req.lineAddr)) {
        req.pwc_hit = true;
        return accLat;
    }
    array->insert(req.lineAddr);
    return invLat + accLat;
}

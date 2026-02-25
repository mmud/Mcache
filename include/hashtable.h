#pragma once
#include <stdint.h>
#include <stddef.h>

class HashTable {
public:
    static const size_t k_max_load_factor = 8;
    static const size_t k_rehashing_work = 128;
    HashTable() {}
    ~HashTable() {}

    struct HNode {
        HNode* next = NULL;
        uint64_t hcode = 0; // hash value
    };

    struct HTab {
        HNode** tab = NULL; // array of slots
        size_t mask = 0;    // power of 2 array size, 2^n - 1
        size_t size = 0;    // number of keys
    };

    struct HMap {
        HTab newer;
        HTab older;
        size_t migrate_pos = 0;
    };

    HNode* hm_lookup(HMap* hmap, HNode* key, bool (*eq)(HNode*, HNode*));
    void   hm_insert(HMap* hmap, HNode* node);
    HNode* hm_delete(HMap* hmap, HNode* key, bool (*eq)(HNode*, HNode*));
    void   hm_clear(HMap* hmap);
    size_t hm_size(HMap* hmap);
    void hm_foreach(HMap* hmap, bool (*f)(HNode*, void*), void* arg);

private:
    static void h_init(HTab* htab, size_t n);
    static void h_insert(HTab* htab, HNode* node);
    static HNode** h_lookup(HTab* htab, HNode* key, bool (*eq)(HNode*, HNode*));
    static HNode* h_detach(HTab* htab, HNode** from);

    static void hm_trigger_rehashing(HMap* hmap);
    static void hm_help_rehashing(HMap* hmap);
    static bool h_foreach(HTab* htab, bool (*f)(HNode*, void*), void* arg);
};
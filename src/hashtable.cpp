#include <assert.h>
#include <stdlib.h>
#include "hashtable.h"

void HashTable::h_init(HashTable::HTab* htab, size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0);    // n must be a power of 2
    htab->tab = (HashTable::HNode**)calloc(n, sizeof(HashTable::HNode*));
    htab->mask = n - 1;
    htab->size = 0;
}

void HashTable::h_insert(HashTable::HTab* htab, HashTable::HNode* node) {
    size_t pos = node->hcode & htab->mask;  // node->hcode & (n - 1)
    HashTable::HNode* next = htab->tab[pos];
    node->next = next;
    htab->tab[pos] = node;
    htab->size++;
}

HashTable::HNode** HashTable::h_lookup(HashTable::HTab* htab, HashTable::HNode* key, bool (*eq)(HashTable::HNode*, HashTable::HNode*)) {
    if (!htab->tab) {
        return NULL;
    }
    size_t pos = key->hcode & htab->mask;
    HashTable::HNode** from = &htab->tab[pos];     // incoming pointer to the target
	for (HashTable::HNode* cur; (cur = *from) != NULL; from = &cur->next) {//pointer-to-pointer traversal
        if (cur->hcode == key->hcode && eq(cur, key)) {
			return from;    //return parent for easy deletion
        }
    }
    return NULL;
}

HashTable::HNode* HashTable::h_detach(HashTable::HTab* htab, HashTable::HNode** from) {
    HashTable::HNode* node = *from;    // the target node
    *from = node->next;     // update the incoming pointer to the target
    htab->size--;
    return node;
}

void HashTable::hm_trigger_rehashing(HashTable::HMap * hmap) {
    hmap->older = hmap->newer;  // (newer, older) <- (new_table, newer)
    h_init(&hmap->newer, (hmap->newer.mask + 1) * 2);
    hmap->migrate_pos = 0;
}

void HashTable::hm_help_rehashing(HashTable::HMap* hmap) {
    size_t nwork = 0;
    while (nwork < HashTable::k_rehashing_work && hmap->older.size > 0) {
        // find a non-empty slot
        HashTable::HNode** from = &hmap->older.tab[hmap->migrate_pos];
        if (!*from) {
            hmap->migrate_pos++;
            continue;   // empty slot
        }
        // move the first list item to the newer table
        h_insert(&hmap->newer, h_detach(&hmap->older, from));
        nwork++;
    }
    // discard the old table if done
    if (hmap->older.size == 0 && hmap->older.tab) {
        free(hmap->older.tab);
        hmap->older = HashTable::HTab{};
    }
}

HashTable::HNode* HashTable::hm_lookup(HashTable::HMap* hmap, HashTable::HNode* key, bool (*eq)(HashTable::HNode*, HashTable::HNode*)) {
    hm_help_rehashing(hmap);
    HashTable::HNode** from = h_lookup(&hmap->newer, key, eq);
    if (!from) {
        from = h_lookup(&hmap->older, key, eq);
    }
    return from ? *from : NULL;
}

HashTable::HNode* HashTable::hm_delete(HashTable::HMap* hmap, HashTable::HNode* key, bool (*eq)(HashTable::HNode*, HashTable::HNode*)) {
    hm_help_rehashing(hmap);
    if (HashTable::HNode** from = h_lookup(&hmap->newer, key, eq)) {
        return h_detach(&hmap->newer, from);
    }
    if (HashTable::HNode** from = h_lookup(&hmap->older, key, eq)) {
        return h_detach(&hmap->older, from);
    }
    return NULL;
}

void HashTable::hm_insert(HashTable::HMap* hmap, HashTable::HNode* node) {
    if (!hmap->newer.tab) {
        h_init(&hmap->newer, 4);    // initialized it if empty
    }
    h_insert(&hmap->newer, node);   // always insert to the newer table
    if (!hmap->older.tab) {         // check whether we need to rehash
        size_t shreshold = (hmap->newer.mask + 1) * k_max_load_factor;
        if (hmap->newer.size >= shreshold) {
            hm_trigger_rehashing(hmap);
        }
    }
    hm_help_rehashing(hmap);        // migrate some keys
}

void HashTable::hm_clear(HashTable::HMap* hmap) {
    free(hmap->newer.tab);
    free(hmap->older.tab);
    *hmap = HashTable::HMap{};
}

size_t HashTable::hm_size(HashTable::HMap* hmap) {
    return hmap->newer.size + hmap->older.size;
}
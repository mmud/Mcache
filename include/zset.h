#pragma once

#include "avl.h"
#include "hashtable.h"

class ZSet {
public:
    AVLTree::AVLNode* root = NULL;   // index by (score, name)
    HashTable::HMap hmap;              // index by name

    struct ZNode {
        AVLTree::AVLNode tree;
        HashTable::HNode   hmap;
        double  score = 0;
        size_t  len = 0;
        char    name[0];        // flexible array
    };

    static bool   zset_insert(ZSet* zset, const char* name, size_t len, double score);
    static ZNode* zset_lookup(ZSet* zset, const char* name, size_t len);
    static void   zset_delete(ZSet* zset, ZNode* node);
    static ZNode* zset_seekge(ZSet* zset, double score, const char* name, size_t len);
    static void   zset_clear(ZSet* zset);
    static ZNode* znode_offset(ZNode* node, int64_t offset);
    static void   tree_dispose(AVLTree::AVLNode* node);
private:
    static ZNode* znode_new(const char* name, size_t len, double score);
    static void   znode_del(ZSet::ZNode* node);
    static bool   zless(AVLTree::AVLNode* lhs, double score, const char* name, size_t len);
    static bool   zless(AVLTree::AVLNode* lhs, AVLTree::AVLNode* rhs);
    static void   tree_insert(ZSet* zset, ZSet::ZNode* node);
    static void   zset_update(ZSet* zset, ZSet::ZNode* node, double score);
    static bool   hcmp(HashTable::HNode* node, HashTable::HNode* key);

};
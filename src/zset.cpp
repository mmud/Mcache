#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "zset.h"
#include "common.h"


ZSet::ZNode* ZSet::znode_new(const char* name, size_t len, double score) {
    ZSet::ZNode* node = (ZSet::ZNode*)malloc(sizeof(ZSet::ZNode) + len);
    assert(node);   // not a good idea in real projects
    AVLTree::avl_init(&node->tree);
    node->hmap.next = NULL;
    node->hmap.hcode = str_hash((uint8_t*)name, len);
    node->score = score;
    node->len = len;
    memcpy(&node->name[0], name, len);
    return node;
}

void ZSet::znode_del(ZSet::ZNode* node) {
    free(node);
}

size_t min(size_t lhs, size_t rhs) {
    return lhs < rhs ? lhs : rhs;
}

// compare by the (score, name) tuple
bool ZSet::zless(AVLTree::AVLNode* lhs, double score, const char* name, size_t len)
{
    ZSet::ZNode* zl = container_of(lhs, ZSet::ZNode, tree);
    if (zl->score != score) {
        return zl->score < score;
    }
    int rv = memcmp(zl->name, name, min(zl->len, len));
    if (rv != 0) {
        return rv < 0;
    }
    return zl->len < len;
}

bool ZSet::zless(AVLTree::AVLNode* lhs, AVLTree::AVLNode* rhs) {
    ZSet::ZNode* zr = container_of(rhs, ZSet::ZNode, tree);
    return zless(lhs, zr->score, zr->name, zr->len);
}

// insert into the AVL tree
void ZSet::tree_insert(ZSet* zset, ZSet::ZNode* node) {
    AVLTree::AVLNode* parent = NULL;         // insert under this node
    AVLTree::AVLNode** from = &zset->root;   // the incoming pointer to the next node
    while (*from) {                 // tree search
        parent = *from;
        from = ZSet::zless(&node->tree, parent) ? &parent->left : &parent->right;
    }
    *from = &node->tree;            // attach the new node
    node->tree.parent = parent;
    zset->root = AVLTree::avl_fix(&node->tree);
}

// update the score of an existing node
void ZSet::zset_update(ZSet* zset, ZSet::ZNode* node, double score) {
    if (node->score == score) {
        return;
    }
    // detach the tree node
    zset->root = AVLTree::avl_del(&node->tree);
    AVLTree::avl_init(&node->tree);
    // reinsert the tree node
    node->score = score;
    ZSet::tree_insert(zset, node);
}

// add a new (score, name) tuple, or update the score of the existing tuple
bool ZSet::zset_insert(ZSet* zset, const char* name, size_t len, double score) {
    ZSet::ZNode* node = zset_lookup(zset, name, len);
    if (node) {
        ZSet::zset_update(zset, node, score);
        return false;
    }
    else {
        node = ZSet::znode_new(name, len, score);
        HashTable::hm_insert(&zset->hmap, &node->hmap);
        ZSet::tree_insert(zset, node);
        return true;
    }
}

// a helper structure for the hashtable lookup
struct HKey {
    HashTable::HNode node;
    const char* name = NULL;
    size_t len = 0;
};

bool ZSet::hcmp(HashTable::HNode* node, HashTable::HNode* key) {
    ZSet::ZNode* znode = container_of(node, ZNode, hmap);
    HKey* hkey = container_of(key, HKey, node);
    if (znode->len != hkey->len) {
        return false;
    }
    return 0 == memcmp(znode->name, hkey->name, znode->len);
}

// lookup by name
ZSet::ZNode* ZSet::zset_lookup(ZSet* zset, const char* name, size_t len) {
    if (!zset->root) {
        return NULL;
    }

    HKey key;
    key.node.hcode = str_hash((uint8_t*)name, len);
    key.name = name;
    key.len = len;
    HashTable::HNode* found = HashTable::hm_lookup(&zset->hmap, &key.node, &hcmp);
    return found ? container_of(found, ZNode, hmap) : NULL;
}

// delete a node
void ZSet::zset_delete(ZSet* zset, ZSet::ZNode* node) {
    // remove from the hashtable
    HKey key;
    key.node.hcode = node->hmap.hcode;
    key.name = node->name;
    key.len = node->len;
    HashTable::HNode* found = HashTable::hm_delete(&zset->hmap, &key.node, &hcmp);
    assert(found);
    // remove from the tree
    zset->root = AVLTree::avl_del(&node->tree);
    // deallocate the node
    ZSet::znode_del(node);
}

// find the first (score, name) tuple that is >= key.
ZSet::ZNode* ZSet::zset_seekge(ZSet* zset, double score, const char* name, size_t len) {
    AVLTree::AVLNode* found = NULL;
    for (AVLTree::AVLNode* node = zset->root; node; ) {
        if (ZSet::zless(node, score, name, len)) {
            node = node->right; // node < key
        }
        else {
            found = node;       // candidate
            node = node->left;
        }
    }
    return found ? container_of(found, ZNode, tree) : NULL;
}

// offset into the succeeding or preceding node.
ZSet::ZNode* ZSet::znode_offset(ZSet::ZNode* node, int64_t offset) {
    AVLTree::AVLNode* tnode = node ? AVLTree::avl_offset(&node->tree, offset) : NULL;
    return tnode ? container_of(tnode, ZNode, tree) : NULL;
}

void ZSet::tree_dispose(AVLTree::AVLNode* node) {
    if (!node) {
        return;
    }
    ZSet::tree_dispose(node->left);
    ZSet::tree_dispose(node->right);
    ZSet::znode_del(container_of(node, ZNode, tree));
}

// destroy the zset
void ZSet::zset_clear(ZSet* zset) {
    HashTable::hm_clear(&zset->hmap);
    ZSet::tree_dispose(zset->root);
    zset->root = NULL;
}
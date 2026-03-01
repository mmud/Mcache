#pragma once

#include <stdint.h>

class AVLTree {
public:

    struct AVLNode {
        AVLNode* parent = NULL;
        AVLNode* left = NULL;
        AVLNode* right = NULL;
        uint32_t height = 0;
        uint32_t cnt = 0;
    };

public:

    // initialize a standalone node
    static void avl_init(AVLNode* node);

    // insertion (returns new root)
    static AVLNode* avl_insert(AVLNode* root, AVLNode* node, int (*cmp)(AVLNode*, AVLNode*));

    // search
    static AVLNode* avl_find(AVLNode* root, AVLNode* key, int (*cmp)(AVLNode*, AVLNode*));

    // delete node (returns new root)
    static AVLNode* avl_del(AVLNode* node);

    // fix tree upward
    static AVLNode* avl_fix(AVLNode* node);

    static uint32_t avl_height(AVLNode* node);
    static uint32_t avl_cnt(AVLNode* node);

    static AVLNode* avl_offset(AVLNode* node, int64_t offset);

private:
    static void avl_update(AVLNode* node);

    static AVLNode* rot_left(AVLNode* node);
    static AVLNode* rot_right(AVLNode* node);

    static AVLNode* avl_fix_left(AVLNode* node);
    static AVLNode* avl_fix_right(AVLNode* node);

    static AVLNode* avl_del_easy(AVLNode* node);
};
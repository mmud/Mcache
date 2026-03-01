#include "avl.h"
#include <assert.h>

static uint32_t max(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? rhs : lhs;
}

void AVLTree::avl_init(AVLNode* node) {
    node->parent = NULL;
    node->left = NULL;
    node->right = NULL;
    node->height = 1;
    node->cnt = 1;
}

uint32_t AVLTree::avl_height(AVLNode* node) {
    return node ? node->height : 0;
}

uint32_t AVLTree::avl_cnt(AVLNode* node) {
    return node ? node->cnt : 0;
}

void AVLTree::avl_update(AVLNode* node) {
    node->height = 1 + max(avl_height(node->left), avl_height(node->right));
    node->cnt = 1 + avl_cnt(node->left) + avl_cnt(node->right);
}

AVLTree::AVLNode* AVLTree::rot_left(AVLNode* node) {
    AVLNode* parent = node->parent;
    AVLNode* new_node = node->right;
    AVLNode* inner = new_node->left;

    node->right = inner;
    if (inner)
        inner->parent = node;

    new_node->parent = parent;

    new_node->left = node;
    node->parent = new_node;

    avl_update(node);
    avl_update(new_node);

    return new_node;
}

AVLTree::AVLNode* AVLTree::rot_right(AVLNode* node) {
    AVLNode* parent = node->parent;
    AVLNode* new_node = node->left;
    AVLNode* inner = new_node->right;

    node->left = inner;
    if (inner)
        inner->parent = node;

    new_node->parent = parent;

    new_node->right = node;
    node->parent = new_node;

    avl_update(node);
    avl_update(new_node);

    return new_node;
}

AVLTree::AVLNode* AVLTree::avl_fix_left(AVLNode* node) {
    if (avl_height(node->left->left) < avl_height(node->left->right)) {
        node->left = rot_left(node->left);
    }
    return rot_right(node);
}

AVLTree::AVLNode* AVLTree::avl_fix_right(AVLNode* node) {
    if (avl_height(node->right->right) < avl_height(node->right->left)) {
        node->right = rot_right(node->right);
    }
    return rot_left(node);
}

AVLTree::AVLNode* AVLTree::avl_fix(AVLNode* node) {
    while (true) {
        AVLNode** from = &node;
        AVLNode* parent = node->parent;

        if (parent) {
            from = parent->left == node ?
                &parent->left : &parent->right;
        }

        avl_update(node);

        uint32_t l = avl_height(node->left);
        uint32_t r = avl_height(node->right);

        if (l == r + 2) {
            *from = avl_fix_left(node);
        }
        else if (l + 2 == r) {
            *from = avl_fix_right(node);
        }

        if (!parent)
            return *from;

        node = parent;
    }
}

AVLTree::AVLNode* AVLTree::avl_insert(
    AVLNode* root,
    AVLNode* node,
    int (*cmp)(AVLNode*, AVLNode*)) {

    if (!root)
        return node;

    AVLNode* cur = root;
    AVLNode* parent = NULL;

    while (cur) {
        parent = cur;
        if (cmp(node, cur) < 0)
            cur = cur->left;
        else
            cur = cur->right;
    }

    node->parent = parent;

    if (cmp(node, parent) < 0)
        parent->left = node;
    else
        parent->right = node;

    return avl_fix(parent);
}

AVLTree::AVLNode* AVLTree::avl_find(AVLNode* root, AVLNode* key, int (*cmp)(AVLNode*, AVLNode*)) {

    while (root) {
        int c = cmp(key, root);
        if (c == 0)
            return root;
        else if (c < 0)
            root = root->left;
        else
            root = root->right;
    }
    return NULL;
}

AVLTree::AVLNode* AVLTree::avl_del_easy(AVLNode* node) {
    assert(!node->left || !node->right);

    AVLNode* child = node->left ?
        node->left : node->right;
    AVLNode* parent = node->parent;

    if (child)
        child->parent = parent;

    if (!parent)
        return child;

    AVLNode** from = parent->left == node ?
        &parent->left : &parent->right;

    *from = child;

    return avl_fix(parent);
}

AVLTree::AVLNode* AVLTree::avl_del(AVLNode* node) {

    if (!node->left || !node->right)
        return avl_del_easy(node);

    AVLTree::AVLNode* victim = node->right;
    while (victim->left)
        victim = victim->left;

    AVLTree::AVLNode* root = avl_del_easy(victim);

    *victim = *node;

    if (victim->left)
        victim->left->parent = victim;
    if (victim->right)
        victim->right->parent = victim;

    AVLTree::AVLNode** from = &root;
    AVLTree::AVLNode* parent = node->parent;

    if (parent) {
        from = parent->left == node ?
            &parent->left : &parent->right;
    }

    *from = victim;

    return root;
}

AVLTree::AVLNode* AVLTree::avl_offset(AVLTree::AVLNode* node, int64_t offset) {
    int64_t pos = 0;    // the rank difference from the starting node
    while (offset != pos) {
        if (pos < offset && pos + AVLTree::avl_cnt(node->right) >= offset) {
            // the target is inside the right subtree
            node = node->right;
            pos += AVLTree::avl_cnt(node->left) + 1;
        }
        else if (pos > offset && pos - AVLTree::avl_cnt(node->left) <= offset) {
            // the target is inside the left subtree
            node = node->left;
            pos -= AVLTree::avl_cnt(node->right) + 1;
        }
        else {
            // go to the parent
            AVLTree::AVLNode* parent = node->parent;
            if (!parent) {
                return NULL;
            }
            if (parent->right == node) {
                pos -= AVLTree::avl_cnt(node->left) + 1;
            }
            else {
                pos += AVLTree::avl_cnt(node->right) + 1;
            }
            node = parent;
        }
    }
    return node;
}
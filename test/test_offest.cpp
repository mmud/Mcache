#include <assert.h>
#include <stddef.h> // REQUIRED for offsetof
#include <stdint.h>
#include "avl.h"

// Corrected container_of for C++ compatibility
#define container_of(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof(type, member)))

struct Data {
    AVLTree::AVLNode node;
    uint32_t val = 0;
};

struct Container {
    AVLTree::AVLNode* root = NULL;
};

static void add(Container& c, uint32_t val) {
    Data* data = new Data();
    AVLTree::avl_init(&data->node);
    data->val = val;

    if (!c.root) {
        c.root = &data->node;
        return;
    }

    AVLTree::AVLNode* cur = c.root;
    while (true) {
        // Use container_of to access the value in the Data struct
        AVLTree::AVLNode** from =
            (val < container_of(cur, Data, node)->val)
            ? &cur->left : &cur->right;
        if (!*from) {
            *from = &data->node;
            data->node.parent = cur;
            c.root = AVLTree::avl_fix(&data->node);
            break;
        }
        cur = *from;
    }
}

static void dispose(AVLTree::AVLNode* node) {
    if (node) {
        dispose(node->left);
        dispose(node->right);
        delete container_of(node, Data, node);
    }
}

static void test_case(uint32_t sz) {
    Container c;
    for (uint32_t i = 0; i < sz; ++i) {
        add(c, i);
    }

    AVLTree::AVLNode* min = c.root;
    while (min && min->left) {
        min = min->left;
    }

    for (uint32_t i = 0; i < sz; ++i) {
        // Corrected: Added AVLTree:: namespace
        AVLTree::AVLNode* node = AVLTree::avl_offset(min, (int64_t)i);
        assert(node != NULL);
        assert(container_of(node, Data, node)->val == i);

        for (uint32_t j = 0; j < sz; ++j) {
            int64_t offset = (int64_t)j - (int64_t)i;
            // Corrected: Added AVLTree:: namespace
            AVLTree::AVLNode* n2 = AVLTree::avl_offset(node, offset);
            assert(n2 != NULL);
            assert(container_of(n2, Data, node)->val == j);
        }
        // Corrected: Added AVLTree:: namespace
        assert(!AVLTree::avl_offset(node, -(int64_t)i - 1));
        assert(!AVLTree::avl_offset(node, sz - i));
    }

    dispose(c.root);
}

int main() {
    for (uint32_t i = 1; i < 500; ++i) {
        test_case(i);
    }
    return 0;
}
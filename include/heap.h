#pragma once

#include <stddef.h>
#include <stdint.h>


class Heap {
public:
    struct HeapItem {
        uint64_t val = 0;
        size_t * ref = NULL;
    };

    static size_t heap_parent(size_t i);
    static size_t heap_left(size_t i);
	static size_t heap_right(size_t i);
    static void heap_up(HeapItem* a, size_t pos);
    static void heap_down(HeapItem* a, size_t pos, size_t len);
    static void heap_update(HeapItem* a, size_t pos, size_t len);

};
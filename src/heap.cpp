#include "heap.h"


size_t Heap::heap_parent(size_t i) {
    return (i + 1) / 2 - 1;
}

size_t Heap::heap_left(size_t i) {
    return i * 2 + 1;
}

size_t Heap::heap_right(size_t i) {
    return i * 2 + 2;
}

void Heap::heap_up(Heap::HeapItem* a, size_t pos) {
    Heap::HeapItem t = a[pos];
    while (pos > 0 && a[heap_parent(pos)].val > t.val) {
        // swap with the parent
        a[pos] = a[heap_parent(pos)];
        *a[pos].ref = pos;
        pos = heap_parent(pos);
    }
    a[pos] = t;
    *a[pos].ref = pos;
}

void Heap::heap_down(Heap::HeapItem* a, size_t pos, size_t len) {
    Heap::HeapItem t = a[pos];
    while (true) {
        // find the smallest one among the parent and their kids
        size_t l = heap_left(pos);
        size_t r = heap_right(pos);
        size_t min_pos = pos;
        uint64_t min_val = t.val;
        if (l < len && a[l].val < min_val) {
            min_pos = l;
            min_val = a[l].val;
        }
        if (r < len && a[r].val < min_val) {
            min_pos = r;
        }
        if (min_pos == pos) {
            break;
        }
        // swap with the kid
        a[pos] = a[min_pos];
        *a[pos].ref = pos;
        pos = min_pos;
    }
    a[pos] = t;
    *a[pos].ref = pos;
}

void Heap::heap_update(Heap::HeapItem* a, size_t pos, size_t len) {
    if (pos > 0 && a[heap_parent(pos)].val > a[pos].val) {
        heap_up(a, pos);
    }
    else {
        heap_down(a, pos, len);
    }
}
#pragma once

#include <stddef.h>

class List {
public:
    struct DList {
        DList* prev = NULL;
        DList* next = NULL;
    };

    static void dlist_init(DList* node);

    static bool dlist_empty(DList* node);

    static void dlist_detach(DList* node);

    static void dlist_insert_before(DList* target, DList* rookie);
};
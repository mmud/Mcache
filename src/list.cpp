#include "list.h"

void List::dlist_init(DList* node) {
    node->prev = node->next = node;
}

bool List::dlist_empty(DList* node) {
    return node->next == node;
}

void List::dlist_detach(DList* node) {
    DList* prev = node->prev;
    DList* next = node->next;
    prev->next = next;
    next->prev = prev;
}

void List::dlist_insert_before(DList* target, DList* rookie) {
    DList* prev = target->prev;
    prev->next = rookie;
    rookie->prev = prev;
    rookie->next = target;
    target->prev = rookie;
}

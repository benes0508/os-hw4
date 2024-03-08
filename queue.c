#include <stdlib.h>
#include <stdbool.h>
#include <threads.h>
#include <stdatomic.h>

typedef struct Item {
    void* content;
    struct Item* forward;
} Item;

typedef struct {
    Item* entry;
    Item* exit;
    mtx_t access_control;
    cnd_t available;
    atomic_size_t quantity;
    atomic_size_t awaiting;
    atomic_size_t retrieved;
} ThreadSafeList;

ThreadSafeList queue;

void initQueue(void) {
    queue.entry = NULL;
    queue.exit = NULL;
    atomic_store(&queue.quantity, 0);
    atomic_store(&queue.awaiting, 0);
    atomic_store(&queue.retrieved, 0);
    mtx_init(&queue.access_control, mtx_plain);
    cnd_init(&queue.available);
}

void destroyQueue(void) {
    mtx_lock(&queue.access_control);
    Item* scan = queue.entry;
    while (scan != NULL) {
        Item* releasable = scan;
        scan = scan->forward;
        free(releasable); 
    }
    queue.entry = NULL;
    queue.exit = NULL;
    mtx_unlock(&queue.access_control);
    
    mtx_destroy(&queue.access_control);
    cnd_destroy(&queue.available);
}

void enqueue(void* data) {
    Item* freshItem = malloc(sizeof(Item));
    if (!freshItem) return;
    
    freshItem->content = data;
    freshItem->forward = NULL;

    mtx_lock(&queue.access_control);
    if (queue.exit == NULL) {
        queue.entry = freshItem;
    } else {
        queue.exit->forward = freshItem;
    }
    queue.exit = freshItem;
    atomic_fetch_add(&queue.quantity, 1);

    if (atomic_load(&queue.awaiting) > 0) {
        cnd_signal(&queue.available);
    }

    mtx_unlock(&queue.access_control);
}

void* dequeue(void) {
    mtx_lock(&queue.access_control);
    while (queue.entry == NULL) {
        atomic_fetch_add(&queue.awaiting, 1);
        cnd_wait(&queue.available, &queue.access_control);
        atomic_fetch_sub(&queue.awaiting, 1);
    }

    Item* item = queue.entry;
    void* data = item->content;
    queue.entry = item->forward;
    if (queue.entry == NULL) {
        queue.exit = NULL;
    }

    free(item);
    atomic_fetch_sub(&queue.quantity, 1);
    atomic_fetch_add(&queue.retrieved, 1);

    mtx_unlock(&queue.access_control);
    return data;
}

bool tryDequeue(void** data) {
    if (mtx_trylock(&queue.access_control) != thrd_success) {
        return false;
    }

    if (queue.entry == NULL) {
        mtx_unlock(&queue.access_control);
        return false;
    }

    Item* item = queue.entry;
    *data = item->content;
    queue.entry = item->forward;
    if (queue.entry == NULL) {
        queue.exit = NULL;
    }

    free(item);
    atomic_fetch_sub(&queue.quantity, 1);
    atomic_fetch_add(&queue.retrieved, 1);

    mtx_unlock(&queue.access_control);
    return true;
}

size_t size(void) {
    return atomic_load(&queue.quantity);
}

size_t waiting(void) {
    return atomic_load(&queue.awaiting);
}

size_t visited(void) {
    return atomic_load(&queue.retrieved);
}

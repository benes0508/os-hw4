#include <stdlib.h>
#include <stdbool.h>
#include <threads.h>
#include <stdatomic.h>

typedef struct Node {
    void* data;
    struct Node* next;
} Node;

typedef struct {
    mtx_t lock;
    cnd_t has_items;
    Node* head;
    Node* tail;
    atomic_size_t size;
    atomic_size_t visited;
    atomic_size_t waiting;
    atomic_uint current_ticket;
    atomic_uint next_ticket;
} ConcurrentQueue;

ConcurrentQueue queue;

void initQueue(void) {
    queue.head = NULL;
    queue.tail = NULL;
    mtx_init(&queue.lock, mtx_plain);
    cnd_init(&queue.has_items);
    atomic_store(&queue.size, 0);
    atomic_store(&queue.visited, 0);
    atomic_store(&queue.waiting, 0);
    atomic_store(&queue.current_ticket, 0);
    atomic_store(&queue.next_ticket, 0);
}

void destroyQueue(void) {
    Node* node = queue.head;
    while (node) {
        Node* tmp = node;
        node = node->next;
        free(tmp);
    }

    mtx_destroy(&queue.lock);
    cnd_destroy(&queue.has_items);
}

void enqueue(void* item) {
    Node* newNode = malloc(sizeof(Node));
    newNode->data = item;
    newNode->next = NULL;

    mtx_lock(&queue.lock);

    if (!queue.tail) {
        queue.head = newNode;
    } else {
        queue.tail->next = newNode;
    }
    queue.tail = newNode;

    atomic_fetch_add(&queue.size, 1);

    cnd_broadcast(&queue.has_items);

    mtx_unlock(&queue.lock);
}

void* dequeue(void) {
    unsigned int my_ticket = atomic_fetch_add(&queue.next_ticket, 1);

    mtx_lock(&queue.lock);

    while (queue.head == NULL || my_ticket != atomic_load(&queue.current_ticket)) {
        atomic_fetch_add(&queue.waiting, 1);
        cnd_wait(&queue.has_items, &queue.lock);
        atomic_fetch_sub(&queue.waiting, 1);
    }

    Node* node = queue.head;
    void* item = node->data;
    queue.head = node->next;

    if (!queue.head) {
        queue.tail = NULL;
    }

    free(node);
    atomic_fetch_sub(&queue.size, 1);
    atomic_fetch_add(&queue.visited, 1);
    atomic_fetch_add(&queue.current_ticket, 1);

    cnd_broadcast(&queue.has_items); // Notify any waiting threads to check their tickets.

    mtx_unlock(&queue.lock);
    return item;
}

bool tryDequeue(void** item) {
    if (mtx_trylock(&queue.lock) == thrd_success) {
        if (!queue.head || atomic_load(&queue.current_ticket) != atomic_load(&queue.next_ticket)) {
            mtx_unlock(&queue.lock);
            return false;
        }

        Node* node = queue.head;
        *item = node->data;
        queue.head = node->next;

        if (!queue.head) {
            queue.tail = NULL;
        }

        free(node);
        atomic_fetch_sub(&queue.size, 1);
        atomic_fetch_add(&queue.visited, 1);
        atomic_fetch_add(&queue.current_ticket, 1); // This is important even for tryDequeue.

        mtx_unlock(&queue.lock);
        return true;
    }

    return false;
}

size_t size(void) {
    return atomic_load(&queue.size);
}

size_t visited(void) {
    return atomic_load(&queue.visited);
}

size_t waiting(void) {
    return atomic_load(&queue.waiting);
}

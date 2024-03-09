#include <stdlib.h>
#include <stdbool.h>
#include <threads.h>
#include <stdatomic.h>

typedef struct Node {
    void* data;
    struct Node* next;
} Node;

typedef struct {
    Node* head;
    Node* tail;
} ItemQueue;

typedef struct {
    mtx_t lock;
    cnd_t has_items;
    ItemQueue items;
    atomic_size_t waiting;
    atomic_size_t visited;
} ConcurrentQueue;

ConcurrentQueue queue;

void initQueue(void) {
    queue.items.head = NULL;
    queue.items.tail = NULL;
    mtx_init(&queue.lock, mtx_plain);
    cnd_init(&queue.has_items, NULL);
    atomic_store(&queue.waiting, 0);
    atomic_store(&queue.visited, 0);
}

void destroyQueue(void) {
    mtx_lock(&queue.lock);

    Node* node = queue.items.head;
    while (node) {
        Node* tmp = node;
        node = node->next;
        free(tmp);
    }

    mtx_unlock(&queue.lock);
    mtx_destroy(&queue.lock);
    cnd_destroy(&queue.has_items);
}

void enqueue(void* item) {
    mtx_lock(&queue.lock);

    Node* newNode = (Node*)malloc(sizeof(Node));
    newNode->data = item;
    newNode->next = NULL;

    if (queue.items.tail) {
        queue.items.tail->next = newNode;
    } else {
        queue.items.head = newNode;
    }
    queue.items.tail = newNode;

    cnd_signal(&queue.has_items);
    mtx_unlock(&queue.lock);
}

void* dequeue(void) {
    mtx_lock(&queue.lock);

    while (!queue.items.head) {
        atomic_fetch_add(&queue.waiting, 1);
        cnd_wait(&queue.has_items, &queue.lock);
        atomic_fetch_sub(&queue.waiting, 1);
    }

    Node* node = queue.items.head;
    void* item = node->data;
    queue.items.head = node->next;

    if (!queue.items.head) {
        queue.items.tail = NULL;
    }

    free(node);
    atomic_fetch_add(&queue.visited, 1);

    mtx_unlock(&queue.lock);
    return item;
}

bool tryDequeue(void** item) {
    if (mtx_trylock(&queue.lock) != thrd_success) {
        return false;
    }

    if (!queue.items.head) {
        mtx_unlock(&queue.lock);
        return false;
    }

    Node* node = queue.items.head;
    *item = node->data;
    queue.items.head = node->next;

    if (!queue.items.head) {
        queue.items.tail = NULL;
    }

    free(node);
    atomic_fetch_add(&queue.visited, 1);

    mtx_unlock(&queue.lock);
    return true;
}

size_t size(void) {
    size_t count = 0;
    mtx_lock(&queue.lock);
    Node* current = queue.items.head;
    while (current) {
        count++;
        current = current->next;
    }
    mtx_unlock(&queue.lock);
    return count;
}

size_t waiting(void) {
    return atomic_load(&queue.waiting);
}

size_t visited(void) {
    return atomic_load(&queue.visited);
}

#include <stdlib.h>
#include <stdbool.h>
#include <threads.h>
#include <stdatomic.h>
#include <semaphore.h>

typedef struct Node {
    void* data;
    struct Node* next;
} Node;

typedef struct {
    Node* head;
    Node* tail;
    mtx_t lock;
    sem_t items_available;  // Semaphore to signal available items
    atomic_size_t size;
    atomic_size_t visited;
    atomic_size_t waiting;  // To keep track of the waiting threads.
} ConcurrentQueue;

ConcurrentQueue queue;

void initQueue(void) {
    queue.head = NULL;
    queue.tail = NULL;
    mtx_init(&queue.lock, mtx_plain);
    sem_init(&queue.items_available, 0, 0);  // Initialize semaphore with 0
    atomic_store(&queue.size, 0);
    atomic_store(&queue.visited, 0);
    atomic_store(&queue.waiting, 0);  // Initialize waiting count.
}

void destroyQueue(void) {
    Node* current;
    mtx_lock(&queue.lock);
    while ((current = queue.head) != NULL) {
        queue.head = current->next;
        free(current);
    }
    queue.tail = NULL;
    mtx_unlock(&queue.lock);
    mtx_destroy(&queue.lock);
    sem_destroy(&queue.items_available);
}

void enqueue(void* item) {
    Node* newNode = malloc(sizeof(Node));
    newNode->data = item;
    newNode->next = NULL;

    mtx_lock(&queue.lock);
    if (queue.tail) {
        queue.tail->next = newNode;
    } else {
        queue.head = newNode;
    }
    queue.tail = newNode;
    atomic_fetch_add(&queue.size, 1);
    if (atomic_load(&queue.waiting) > 0) {
        sem_post(&queue.items_available);  // Signal available item
    }
    mtx_unlock(&queue.lock);
}

void* dequeue(void) {
    sem_wait(&queue.items_available);  // Wait for available item
    mtx_lock(&queue.lock);
    Node* node = queue.head;
    queue.head = node->next;
    if (!queue.head) {
        queue.tail = NULL;
    }
    void* item = node->data;
    free(node);
    atomic_fetch_sub(&queue.size, 1);
    atomic_fetch_add(&queue.visited, 1);
    mtx_unlock(&queue.lock);
    return item;
}

bool tryDequeue(void** item) {
    if (mtx_trylock(&queue.lock) != thrd_success) {
        return false;
    }
    if (!queue.head) {
        mtx_unlock(&queue.lock);
        return false;
    }
    Node* node = queue.head;
    queue.head = node->next;
    if (!queue.head) {
        queue.tail = NULL;
    }
    *item = node->data;
    free(node);
    atomic_fetch_sub(&queue.size, 1);
    atomic_fetch_add(&queue.visited, 1);
    mtx_unlock(&queue.lock);
    return true;
}

size_t size(void) {
    return atomic_load(&queue.size);
}

size_t waiting(void) {
    return atomic_load(&queue.waiting);
}

size_t visited(void) {
    return atomic_load(&queue.visited);
}

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
    mtx_t lock;
    cnd_t not_empty;
    atomic_size_t size;
    atomic_size_t visited;
} ConcurrentQueue;

ConcurrentQueue queue;

void initQueue(void) {
    queue.head = NULL;
    queue.tail = NULL;
    mtx_init(&queue.lock, mtx_plain);
    cnd_init(&queue.not_empty);
    atomic_store(&queue.size, 0);
    atomic_store(&queue.visited, 0);
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
    cnd_destroy(&queue.not_empty);
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
    cnd_signal(&queue.not_empty);
    mtx_unlock(&queue.lock);
}

void* dequeue(void) {
    mtx_lock(&queue.lock);
    while (!queue.head) {
        cnd_wait(&queue.not_empty, &queue.lock);
    }
    Node* temp = queue.head;
    queue.head = queue.head->next;
    if (!queue.head) {
        queue.tail = NULL;
    }
    void* item = temp->data;
    free(temp);
    atomic_fetch_sub(&queue.size, 1);
    atomic_fetch_add(&queue.visited, 1);
    mtx_unlock(&queue.lock);
    return item;
}

size_t size(void) {
    return atomic_load(&queue.size);
}

size_t visited(void) {
    return atomic_load(&queue.visited);
}

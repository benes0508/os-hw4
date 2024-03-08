#include <stdlib.h>
#include <stdbool.h>
#include <threads.h>
#include <stdatomic.h>
#include "queue.h"

typedef struct Node {
    void* data;
    struct Node* next;
} Node;

typedef struct WaitNode {
    cnd_t cond;
    struct WaitNode* next;
} WaitNode;

typedef struct {
    Node* head;
    Node* tail;
    WaitNode* waitHead;
    WaitNode* waitTail;
    mtx_t lock;
    atomic_size_t size;
    atomic_size_t waiting;
    atomic_size_t visited;
} ConcurrentQueue;

ConcurrentQueue queue;

void initQueue(void) {
    queue.head = NULL;
    queue.tail = NULL;
    queue.waitHead = NULL;
    queue.waitTail = NULL;
    atomic_store(&queue.size, 0);
    atomic_store(&queue.waiting, 0);
    atomic_store(&queue.visited, 0);
    mtx_init(&queue.lock, mtx_plain);
}

void destroyQueue(void) {
    mtx_lock(&queue.lock);
    while (queue.head) {
        Node* tmp = queue.head;
        queue.head = queue.head->next;
        free(tmp);
    }
    while (queue.waitHead) {
        WaitNode* tmp = queue.waitHead;
        queue.waitHead = queue.waitHead->next;
        cnd_destroy(&tmp->cond);
        free(tmp);
    }
    mtx_unlock(&queue.lock);
    mtx_destroy(&queue.lock);
}

void enqueue(void* item) {
    mtx_lock(&queue.lock);
    Node* newNode = malloc(sizeof(Node));
    newNode->data = item;
    newNode->next = NULL;

    if (!queue.head) {
        queue.head = newNode;
    } else {
        queue.tail->next = newNode;
    }
    queue.tail = newNode;
    atomic_fetch_add(&queue.size, 1);

    if (queue.waitHead) {
        cnd_signal(&queue.waitHead->cond);
    }

    mtx_unlock(&queue.lock);
}

void* dequeue(void) {
    mtx_lock(&queue.lock);
    while (!queue.head) {
        if (!queue.waitHead) {
            WaitNode* newWaitNode = malloc(sizeof(WaitNode));
            cnd_init(&newWaitNode->cond);
            newWaitNode->next = NULL;

            queue.waitHead = newWaitNode;
            queue.waitTail = newWaitNode;
        } else {
            WaitNode* newWaitNode = malloc(sizeof(WaitNode));
            cnd_init(&newWaitNode->cond);
            newWaitNode->next = NULL;

            queue.waitTail->next = newWaitNode;
            queue.waitTail = newWaitNode;
        }
        atomic_fetch_add(&queue.waiting, 1);
        cnd_wait(&queue.waitHead->cond, &queue.lock);
        atomic_fetch_sub(&queue.waiting, 1);

        // Move the waitHead to the next node after the thread wakes up.
        WaitNode* temp = queue.waitHead;
        queue.waitHead = queue.waitHead->next;
        cnd_destroy(&temp->cond);
        free(temp);
        if (!queue.waitHead) queue.waitTail = NULL;
    }

    Node* node = queue.head;
    queue.head = queue.head->next;
    if (!queue.head) queue.tail = NULL;

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
    queue.head = queue.head->next;
    if (!queue.head) queue.tail = NULL;

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

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
    Node* node = queue.head;
    while (node != NULL) {
        Node* temp = node;
        node = node->next;
        free(temp);
    }
    queue.head = NULL;
    queue.tail = NULL;

    WaitNode* waitNode = queue.waitHead;
    while (waitNode != NULL) {
        WaitNode* temp = waitNode;
        waitNode = waitNode->next;
        cnd_destroy(&temp->cond);
        free(temp);
    }
    queue.waitHead = NULL;
    queue.waitTail = NULL;

    mtx_unlock(&queue.lock);
    mtx_destroy(&queue.lock);
}

void enqueue(void* item) {
    Node* newNode = malloc(sizeof(Node));
    newNode->data = item;
    newNode->next = NULL;

    mtx_lock(&queue.lock);
    if (queue.tail != NULL) {
        queue.tail->next = newNode;
    } else {
        queue.head = newNode;
    }
    queue.tail = newNode;
    atomic_fetch_add(&queue.size, 1);

    if (queue.waitHead != NULL) {
        WaitNode* waitNode = queue.waitHead;
        cnd_signal(&waitNode->cond);
        queue.waitHead = waitNode->next;
        if (queue.waitHead == NULL) {
            queue.waitTail = NULL;
        }
        cnd_destroy(&waitNode->cond);
        free(waitNode);
    }

    mtx_unlock(&queue.lock);
}

void* dequeue(void) {
    mtx_lock(&queue.lock);
    while (queue.head == NULL) {
        WaitNode* newWaitNode = malloc(sizeof(WaitNode));
        cnd_init(&newWaitNode->cond);
        newWaitNode->next = NULL;

        if (queue.waitTail != NULL) {
            queue.waitTail->next = newWaitNode;
        } else {
            queue.waitHead = newWaitNode;
        }
        queue.waitTail = newWaitNode;

        atomic_fetch_add(&queue.waiting, 1);
        cnd_wait(&newWaitNode->cond, &queue.lock);
        atomic_fetch_sub(&queue.waiting, 1);
    }

    Node* temp = queue.head;
    void* item = temp->data;
    queue.head = temp->next;
    if (queue.head == NULL) {
        queue.tail = NULL;
    }

    free(temp);
    atomic_fetch_sub(&queue.size, 1);
    atomic_fetch_add(&queue.visited, 1);
    mtx_unlock(&queue.lock);
    return item;
}

bool tryDequeue(void** item) {
    if (mtx_trylock(&queue.lock) != thrd_success) {
        return false;
    }

    if (queue.head == NULL) {
        mtx_unlock(&queue.lock);
        return false;
    }

    Node* temp = queue.head;
    *item = temp->data;
    queue.head = temp->next;
    if (queue.head == NULL) {
        queue.tail = NULL;
    }

    free(temp);
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

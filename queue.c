#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

// Represents each item in the queue.
typedef struct ItemNode {
    void* payload; // The data stored in this node.
    struct ItemNode* next; // Pointer to the next node in the queue.
} ItemNode;

// Manages threads that are waiting for the queue to have items.
typedef struct ThreadWaitNode {
    cnd_t waitCondition; // Condition variable for waiting.
    struct ThreadWaitNode* next; // Next thread in the waiting list.
} ThreadWaitNode;

// The main structure for the queue, renamed for clarity.
typedef struct {
    ItemNode* front; // Start of the queue.
    ItemNode* rear; // End of the queue.
    mtx_t syncLock; // Mutex for synchronizing access to the queue.
    cnd_t itemAvailable; // Condition variable for when the queue is not empty.
    atomic_size_t itemCount; // Current count of items in the queue.
    atomic_size_t waitingCount; // Number of threads waiting for an item.
    atomic_size_t processedCount; // Count of items processed through the queue.
    ThreadWaitNode* waitListFront; // Start of the waiting threads list.
    ThreadWaitNode* waitListRear; // End of the waiting threads list.
    mtx_t waitListLock; // Mutex for synchronizing access to the waiting threads list.
} SyncQueue;

SyncQueue myQueue;

void initQueue(void) {
    myQueue.front = myQueue.rear = NULL;
    mtx_init(&myQueue.syncLock, mtx_plain);
    cnd_init(&myQueue.itemAvailable);
    atomic_store(&myQueue.itemCount, 0);
    atomic_store(&myQueue.waitingCount, 0);
    atomic_store(&myQueue.processedCount, 0);
    myQueue.waitListFront = myQueue.waitListRear = NULL;
    mtx_init(&myQueue.waitListLock, mtx_plain);
}

void destroyQueue(void) {
    ItemNode* currentNode = myQueue.front;
    while (currentNode) {
        ItemNode* toFree = currentNode;
        currentNode = currentNode->next;
        free(toFree);
    }
    
    ThreadWaitNode* currentWaitNode = myQueue.waitListFront;
    while (currentWaitNode) {
        ThreadWaitNode* toFree = currentWaitNode;
        currentWaitNode = currentWaitNode->next;
        cnd_destroy(&toFree->waitCondition);
        free(toFree);
    }

    mtx_destroy(&myQueue.syncLock);
    cnd_destroy(&myQueue.itemAvailable);
    mtx_destroy(&myQueue.waitListLock);
}

void enqueue(void* data) {
    ItemNode* newNode = malloc(sizeof(ItemNode));
    newNode->payload = data;
    newNode->next = NULL;

    mtx_lock(&myQueue.syncLock);

    if (myQueue.rear == NULL) {
        myQueue.front = myQueue.rear = newNode;
    } else {
        myQueue.rear->next = newNode;
        myQueue.rear = newNode;
    }

    atomic_fetch_add(&myQueue.itemCount, 1);
    cnd_signal(&myQueue.itemAvailable);

    mtx_unlock(&myQueue.syncLock);
}

void* dequeue(void) {
    mtx_lock(&myQueue.syncLock);

    while (myQueue.front == NULL) {
        cnd_wait(&myQueue.itemAvailable, &myQueue.syncLock);
    }

    ItemNode* tempNode = myQueue.front;
    void* data = tempNode->payload;
    myQueue.front = myQueue.front->next;
    if (myQueue.front == NULL) {
        myQueue.rear = NULL;
    }

    atomic_fetch_sub(&myQueue.itemCount, 1);
    atomic_fetch_add(&myQueue.processedCount, 1);

    free(tempNode);

    mtx_unlock(&myQueue.syncLock);
    return data;
}

bool tryDequeue(void** data) {
    if (mtx_trylock(&myQueue.syncLock) == thrd_success) {
        if (myQueue.front == NULL) {
            mtx_unlock(&myQueue.syncLock);
            return false;
        }

        ItemNode* tempNode = myQueue.front;
        *data = tempNode->payload;
        myQueue.front = myQueue.front->next;
        if (myQueue.front == NULL) {
            myQueue.rear = NULL;
        }

        atomic_fetch_sub(&myQueue.itemCount, 1);
        atomic_fetch_add(&myQueue.processedCount, 1);

        free(tempNode);
        mtx_unlock(&myQueue.syncLock);
        return true;
    }
    return false;
}

size_t size(void) {
    return atomic_load(&myQueue.itemCount);
}

size_t waiting(void) {
    return atomic_load(&myQueue.waitingCount);
}

size_t visited(void) {
    return atomic_load(&myQueue.processedCount);
}

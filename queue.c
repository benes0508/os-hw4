#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

// Renamed structure for each item in the queue.
typedef struct DataNode {
    void* item; // The data stored in this node.
    struct DataNode* nextNode; // Pointer to the next node in the queue.
} DataNode;

// Renamed structure for managing threads that are waiting.
typedef struct WaitNode {
    cnd_t waitCond; // Condition variable for waiting.
    struct WaitNode* nextWait; // Next waiting thread node.
} WaitNode;

// Renamed main structure for the queue.
typedef struct {
    DataNode* first; // Start of the queue.
    DataNode* last; // End of the queue.
    mtx_t lockQueue; // Mutex for synchronizing access to the queue.
    cnd_t notEmptyCond; // Condition variable to wait on when queue is empty.
    atomic_size_t queueSize; // Number of items in the queue.
    atomic_size_t threadsWaiting; // Count of threads waiting for an item.
    atomic_size_t itemsProcessed; // Count of items that have been processed.
    WaitNode* firstWait; // Start of the list of waiting threads.
    WaitNode* lastWait; // End of the list of waiting threads.
} SyncQueue;

SyncQueue syncQueue;

void initQueue(void) {
    syncQueue.first = NULL;
    syncQueue.last = NULL;
    mtx_init(&syncQueue.lockQueue, mtx_plain);
    cnd_init(&syncQueue.notEmptyCond);
    atomic_store(&syncQueue.queueSize, 0);
    atomic_store(&syncQueue.threadsWaiting, 0);
    atomic_store(&syncQueue.itemsProcessed, 0);
    syncQueue.firstWait = NULL;
    syncQueue.lastWait = NULL;
}

void destroyQueue(void) {
    DataNode* current = syncQueue.first;
    while (current) {
        DataNode* temp = current;
        current = current->nextNode;
        free(temp); // Free each node.
    }
    
    WaitNode* currentWait = syncQueue.firstWait;
    while (currentWait) {
        WaitNode* tempWait = currentWait;
        currentWait = currentWait->nextWait;
        cnd_destroy(&tempWait->waitCond); // Clean up each waiting node.
        free(tempWait);
    }

    mtx_destroy(&syncQueue.lockQueue);
    cnd_destroy(&syncQueue.notEmptyCond);
}

void enqueue(void* item) {
    DataNode* newNode = malloc(sizeof(DataNode));
    newNode->item = item;
    newNode->nextNode = NULL;

    mtx_lock(&syncQueue.lockQueue);

    if (syncQueue.last == NULL) {
        syncQueue.first = syncQueue.last = newNode;
    } else {
        syncQueue.last->nextNode = newNode;
        syncQueue.last = newNode;
    }

    atomic_fetch_add(&syncQueue.queueSize, 1);
    cnd_signal(&syncQueue.notEmptyCond);

    mtx_unlock(&syncQueue.lockQueue);
}

void* dequeue(void) {
    mtx_lock(&syncQueue.lockQueue);

    while (syncQueue.first == NULL) {
        cnd_wait(&syncQueue.notEmptyCond, &syncQueue.lockQueue);
    }

    DataNode* node = syncQueue.first;
    syncQueue.first = node->nextNode;
    if (syncQueue.first == NULL) {
        syncQueue.last = NULL;
    }

    void* item = node->item;
    free(node);

    atomic_fetch_sub(&syncQueue.queueSize, 1);
    atomic_fetch_add(&syncQueue.itemsProcessed, 1);

    mtx_unlock(&syncQueue.lockQueue);
    return item;
}

bool tryDequeue(void** item) {
    if (mtx_trylock(&syncQueue.lockQueue) == thrd_success) {
        if (syncQueue.first == NULL) {
            mtx_unlock(&syncQueue.lockQueue);
            return false;
        }

        DataNode* node = syncQueue.first;
        *item = node->item;
        syncQueue.first = node->nextNode;
        if (syncQueue.first == NULL) {
            syncQueue.last = NULL;
        }

        atomic_fetch_sub(&syncQueue.queueSize, 1);
        atomic_fetch_add(&syncQueue.itemsProcessed, 1);

        free(node);
        mtx_unlock(&syncQueue.lockQueue);
        return true;
    }

    return false;
}

size_t size(void) {
    return atomic_load(&syncQueue.queueSize);
}

size_t waiting(void) {
    return atomic_load(&syncQueue.threadsWaiting);
}

size_t visited(void) {
    return atomic_load(&syncQueue.itemsProcessed);
}

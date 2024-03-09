#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

// Renamed structure for queue items.
typedef struct Node {
    void* content; // Pointer to the item's data.
    struct Node* follow; // Next item in the queue.
} Node;

// Renamed structure for managing waiting threads.
typedef struct SleepNode {
    cnd_t condition; // Condition variable for this sleeping thread.
    struct SleepNode* next; // Next node in the list of sleeping threads.
} SleepNode;

// Renamed main queue structure with detailed variable names for clarity.
typedef struct {
    Node* front; // Pointer to the first item in the queue.
    Node* back; // Pointer to the last item in the queue.
    mtx_t lock; // Mutex for synchronizing access to the queue.
    cnd_t hasItems; // Condition variable to signal when items are added.
    atomic_size_t itemCount; // Total number of items in the queue.
    atomic_size_t waitCount; // Number of threads currently waiting for an item.
    atomic_size_t processedCount; // Total number of items that have been processed.
    SleepNode* sleepFront; // Pointer to the first sleeping thread node.
    SleepNode* sleepBack; // Pointer to the last sleeping thread node.
    mtx_t sleepLock; // Mutex for synchronizing access to the sleeping threads list.
} ManagedQueue;

ManagedQueue queue;

void initQueue(void) {
    queue.front = queue.back = NULL;
    mtx_init(&queue.lock, mtx_plain);
    cnd_init(&queue.hasItems);
    atomic_store(&queue.itemCount, 0);
    atomic_store(&queue.waitCount, 0);
    atomic_store(&queue.processedCount, 0);
    queue.sleepFront = queue.sleepBack = NULL;
    mtx_init(&queue.sleepLock, mtx_plain);
}

void destroyQueue(void) {
    Node* current = queue.front;
    while (current != NULL) {
        Node* toDelete = current;
        current = current->follow;
        free(toDelete);
    }

    SleepNode* waitCurrent = queue.sleepFront;
    while (waitCurrent != NULL) {
        SleepNode* toDelete = waitCurrent;
        waitCurrent = waitCurrent->next;
        cnd_destroy(&toDelete->condition);
        free(toDelete);
    }

    mtx_destroy(&queue.lock);
    cnd_destroy(&queue.hasItems);
    mtx_destroy(&queue.sleepLock);
}

void enqueue(void* data) {
    Node* newNode = malloc(sizeof(Node));
    newNode->content = data;
    newNode->follow = NULL;

    mtx_lock(&queue.lock);
    
    if (queue.back == NULL) {
        queue.front = queue.back = newNode;
    } else {
        queue.back->follow = newNode;
        queue.back = newNode;
    }

    atomic_fetch_add(&queue.itemCount, 1);
    cnd_signal(&queue.hasItems);

    mtx_unlock(&queue.lock);
}

void* dequeue(void) {
    mtx_lock(&queue.lock);

    while (queue.front == NULL) {
        cnd_wait(&queue.hasItems, &queue.lock);
    }

    Node* nodeToRemove = queue.front;
    void* data = nodeToRemove->content;
    queue.front = queue.front->follow;
    
    if (queue.front == NULL) {
        queue.back = NULL;
    }

    atomic_fetch_sub(&queue.itemCount, 1);
    atomic_fetch_add(&queue.processedCount, 1);

    free(nodeToRemove);

    mtx_unlock(&queue.lock);
    return data;
}

bool tryDequeue(void** data) {
    if (mtx_trylock(&queue.lock) == thrd_success) {
        if (queue.front == NULL) {
            mtx_unlock(&queue.lock);
            return false;
        }

        Node* nodeToRemove = queue.front;
        *data = nodeToRemove->content;
        queue.front = queue.front->follow;
        
        if (queue.front == NULL) {
            queue.back = NULL;
        }

        atomic_fetch_sub(&queue.itemCount, 1);
        atomic_fetch_add(&queue.processedCount, 1);

        free(nodeToRemove);
        mtx_unlock(&queue.lock);
        return true;
    }

    return false;
}

size_t size(void) {
    return atomic_load(&queue.itemCount);
}

size_t waiting(void) {
    return atomic_load(&queue.waitCount);
}

size_t visited(void) {
    return atomic_load(&queue.processedCount);
}

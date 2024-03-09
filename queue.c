#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

// Structure for each item in the queue.
typedef struct QueueNode {
    void* data; // The data stored in this node.
    struct QueueNode* next; // Pointer to the next node in the queue.
} QueueNode;

// Structure for managing threads that are waiting.
typedef struct WaitingThreadNode {
    cnd_t cond; // Condition variable for waiting.
    struct WaitingThreadNode* next; // Next waiting thread node.
} WaitingThreadNode;

// The main structure for the queue.
typedef struct {
    QueueNode* head; // Start of the queue.
    QueueNode* tail; // End of the queue.
    mtx_t queueMutex; // Mutex for synchronizing access to the queue.
    cnd_t notEmpty; // Condition variable to wait on when queue is empty.
    atomic_size_t size; // Number of items in the queue.
    atomic_size_t waitingThreads; // Count of threads waiting for an item.
    atomic_size_t visitedItems; // Count of items that have been processed.
    WaitingThreadNode* waitingHead; // Start of the list of waiting threads.
    WaitingThreadNode* waitingTail; // End of the list of waiting threads.
    mtx_t waitMutex; // Mutex for synchronizing access to the waiting list.
    atomic_uint currentTicket; // The current ticket number being served.
    atomic_uint nextTicket; // The next ticket number to be issued.
} ConcurrentQueue;

ConcurrentQueue queue;

void initQueue(void) {
    // Initialize the queue when the program starts.
    queue.head = NULL;
    queue.tail = NULL;
    mtx_init(&queue.queueMutex, mtx_plain);
    cnd_init(&queue.notEmpty);
    atomic_store(&queue.size, 0);
    atomic_store(&queue.waitingThreads, 0);
    atomic_store(&queue.visitedItems, 0);
    queue.waitingHead = NULL;
    queue.waitingTail = NULL;
    mtx_init(&queue.waitMutex, mtx_plain);
    atomic_store(&queue.currentTicket, 0);
    atomic_store(&queue.nextTicket, 0);
}

void destroyQueue(void) {
    // Clean up the queue when we're done with it.
    QueueNode* current = queue.head;
    while (current) {
        QueueNode* temp = current;
        current = current->next;
        free(temp); // Free each node.
    }
    
    WaitingThreadNode* currentWaitNode = queue.waitingHead;
    while (currentWaitNode) {
        WaitingThreadNode* temp = currentWaitNode;
        currentWaitNode = currentWaitNode->next;
        cnd_destroy(&temp->cond); // Clean up each waiting node.
        free(temp);
    }

    // Clean up mutexes and condition variables.
    mtx_destroy(&queue.queueMutex);
    cnd_destroy(&queue.notEmpty);
    mtx_destroy(&queue.waitMutex);
}

void enqueue(void* data) {
    // Add an item to the end of the queue.
    QueueNode* newNode = malloc(sizeof(QueueNode));
    if (!newNode) {
        // If we can't allocate memory, print an error message.
        fprintf(stderr, "Failed to allocate memory for new node.\n");
        return;
    }
    newNode->data = data;
    newNode->next = NULL;

    mtx_lock(&queue.queueMutex); // Make sure no one else can modify the queue right now.

    // If the queue is empty, this new node is both the head and tail.
    if (queue.tail == NULL) {
        queue.head = queue.tail = newNode;
        cnd_signal(&queue.notEmpty); // Tell any waiting threads that the queue isn't empty anymore.
    } else {
        queue.tail->next = newNode; // Put the new node at the end of the queue.
        queue.tail = newNode; // Update the end of the queue.
    }

    atomic_fetch_add(&queue.size, 1); // Increase the size of the queue.

    mtx_unlock(&queue.queueMutex); // Let others access the queue again.
}

void* dequeue(void) {
    // Remove an item from the start of the queue.
    mtx_lock(&queue.queueMutex); // Lock the queue so only we can access it.

    // If there's nothing in the queue, wait until there is.
    while (queue.head == NULL) {
        cnd_wait(&queue.notEmpty, &queue.queueMutex);
    }

    // Take the first item off the queue.
    QueueNode* node = queue.head;
    queue.head = node->next; // Move the head pointer to the next item.
    if (queue.head == NULL) {
        queue.tail = NULL; // If the queue is empty now, there's no tail either.
    }

    void* data = node->data; // Grab the data from the node.
    free(node); // We're done with the node structure now.

    atomic_fetch_sub(&queue.size, 1); // There's one less item now.
    atomic_fetch_add(&queue.visitedItems, 1); // We've processed another item.

    // If there's still stuff in the queue, let another waiting thread know.
    if (queue.head != NULL) {
        cnd_signal(&queue.notEmpty);
    }

    mtx_unlock(&queue.queueMutex); // Unlock the queue.
    return data; // Return the data we just dequeued.
}

bool tryDequeue(void** data) {
    // Try to remove an item without waiting.
    if (mtx_trylock(&queue.queueMutex) == thrd_success) {
        // If the queue is empty, just give up.
        if (queue.head == NULL) {
            mtx_unlock(&queue.queueMutex);
            return false;
        }

        // Otherwise, dequeue like normal.
        QueueNode* tempNode = queue.head;
        *data = tempNode->data;
        queue.head = queue.head->next;
        
        if (queue.head == NULL) {
            queue.tail = NULL;
        }

        atomic_fetch_sub(&queue.size, 1);
        atomic_fetch_add(&queue.visitedItems, 1);

        free(tempNode);
        mtx_unlock(&queue.queueMutex);

        return true; // We got something!
    }

    return false; // Couldn't even try to dequeue because the queue was locked.
}

size_t size(void) {
    // How big is the queue?
    return atomic_load(&queue.size);
}

size_t waiting(void) {
    // How many threads are waiting for something to be in the queue?
    return atomic_load(&queue.waitingThreads);
}

size_t visited(void) {
    // How many items have been taken out of the queue?
    return atomic_load(&queue.visitedItems);
}

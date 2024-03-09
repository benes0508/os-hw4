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

typedef struct WaitNode {
    thrd_t thread_id;
    cnd_t cond;
    struct WaitNode* next;
} WaitNode;

typedef struct {
    WaitNode* waitHead;
    WaitNode* waitTail;
    mtx_t lock;
    atomic_size_t waiting;
    atomic_size_t visited;
    ItemQueue items;
} ConcurrentQueue;

ConcurrentQueue queue;

void initQueue(void) {
    queue.items.head = NULL;
    queue.items.tail = NULL;
    atomic_store(&queue.waiting, 0);
    atomic_store(&queue.visited, 0);
    mtx_init(&queue.lock, mtx_plain);
    queue.waitHead = NULL;
    queue.waitTail = NULL;
}

void destroyQueue(void) {
    mtx_lock(&queue.lock);

    Node* node;
    while ((node = queue.items.head) != NULL) {
        queue.items.head = node->next;
        free(node);
    }
    queue.items.tail = NULL;

    WaitNode* waitNode;
    while ((waitNode = queue.waitHead) != NULL) {
        queue.waitHead = waitNode->next;
        cnd_destroy(&waitNode->cond);
        free(waitNode);
    }
    queue.waitTail = NULL;

    mtx_unlock(&queue.lock);
    mtx_destroy(&queue.lock);
}

void enqueue(void* item) {
    mtx_lock(&queue.lock);

    if (queue.waitHead) {
        Node* newNode = malloc(sizeof(Node));
        newNode->data = item;
        newNode->next = NULL;

        // Add item directly to the queue even if there are waiting threads.
        if (queue.items.tail) {
            queue.items.tail->next = newNode;
        } else {
            queue.items.head = newNode;
        }
        queue.items.tail = newNode;

        // Signal the first waiting thread.
        cnd_signal(&queue.waitHead->cond);
    } else {
        Node* newNode = malloc(sizeof(Node));
        newNode->data = item;
        newNode->next = NULL;

        if (queue.items.tail) {
            queue.items.tail->next = newNode;
        } else {
            queue.items.head = newNode;
        }
        queue.items.tail = newNode;
    }

    mtx_unlock(&queue.lock);
}

void* dequeue(void) {
    mtx_lock(&queue.lock);

    while (!queue.items.head) {
        if (!queue.waitTail) {
            WaitNode* waitNode = malloc(sizeof(WaitNode));
            cnd_init(&waitNode->cond);
            waitNode->thread_id = thrd_current();
            waitNode->next = NULL;

            queue.waitHead = waitNode;
            queue.waitTail = waitNode;

            atomic_fetch_add(&queue.waiting, 1);
            cnd_wait(&waitNode->cond, &queue.lock);
            atomic_fetch_sub(&queue.waiting, 1);

            // Remove the wait node after it's been signaled.
            if (queue.waitHead == queue.waitTail) {
                queue.waitHead = queue.waitTail = NULL;
            } else {
                queue.waitHead = queue.waitHead->next;
            }

            free(waitNode);
        }

        // Breaking out to handle dequeuing outside the loop.
        break;
    }

    Node* node = queue.items.head;
    void* item = NULL;
    if (node) {
        queue.items.head = node->next;
        if (!queue.items.head) {
            queue.items.tail = NULL;
        }
        item = node->data;
        free(node);
    }

    mtx_unlock(&queue.lock);
    return item;
}

bool tryDequeue(void** item) {
    mtx_lock(&queue.lock);

    if (!queue.items.head) {
        mtx_unlock(&queue.lock);
        return false;
    }

    Node* node = queue.items.head;
    queue.items.head = node->next;

    if (!queue.items.head) {
        queue.items.tail = NULL;
    }

    *item = node->data;
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
        ++count;
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

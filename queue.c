#include <stdlib.h>
#include <stdbool.h>
#include <threads.h>
#include <stdatomic.h>

typedef struct Node {
    void* data;
    struct Node* next;
} Node;

typedef struct WaitNode {
    thrd_t thread_id;
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
    Node* node;
    while ((node = queue.head) != NULL) {
        queue.head = node->next;
        free(node);
    }
    queue.tail = NULL;

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
    Node* newNode = malloc(sizeof(Node));
    newNode->data = item;
    newNode->next = NULL;

    if (queue.tail) {
        queue.tail->next = newNode;
    } else {
        queue.head = newNode;
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
        WaitNode* newWaitNode = malloc(sizeof(WaitNode));
        cnd_init(&newWaitNode->cond);
        newWaitNode->thread_id = thrd_current();
        newWaitNode->next = NULL;

        if (!queue.waitTail) {
            queue.waitHead = newWaitNode;
            queue.waitTail = newWaitNode;
        } else {
            queue.waitTail->next = newWaitNode;
            queue.waitTail = newWaitNode;
        }

        atomic_fetch_add(&queue.waiting, 1);
        cnd_wait(&newWaitNode->cond, &queue.lock);
        atomic_fetch_sub(&queue.waiting, 1);

        if (newWaitNode == queue.waitHead) {
            queue.waitHead = newWaitNode->next;
            if (queue.waitHead == NULL) {
                queue.waitTail = NULL;
            }
        } else {
            WaitNode* prev = queue.waitHead;
            while (prev && prev->next != newWaitNode) {
                prev = prev->next;
            }
            if (prev) {
                prev->next = newWaitNode->next;
                if (!prev->next) {
                    queue.waitTail = prev;
                }
            }
        }

        free(newWaitNode);
        if (queue.head) {
            break;
        }
    }

    Node* node = queue.head;
    void* item = NULL;
    if (node) {
        queue.head = node->next;
        if (!queue.head) {
            queue.tail = NULL;
        }
        item = node->data;
        free(node);
        atomic_fetch_sub(&queue.size, 1);
        atomic_fetch_add(&queue.visited, 1);
    }

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

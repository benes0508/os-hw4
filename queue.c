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
    atomic_size_t size;
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

    Node* newNode = malloc(sizeof(Node));
    newNode->data = item;
    newNode->next = NULL;

    if (queue.items.tail) {
        queue.items.tail->next = newNode;
    } else {
        queue.items.head = newNode;
    }
    queue.items.tail = newNode;

    if (queue.waitHead) {
        cnd_signal(&queue.waitHead->cond);
    }

    mtx_unlock(&queue.lock);
}

void* dequeue(void) {
    mtx_lock(&queue.lock);

    while (!queue.items.head) {
        if (!queue.waitTail) {
            WaitNode* newWaitNode = malloc(sizeof(WaitNode));
            cnd_init(&newWaitNode->cond);
            newWaitNode->thread_id = thrd_current();
            newWaitNode->next = NULL;
            queue.waitHead = newWaitNode;
            queue.waitTail = newWaitNode;
        } else {
            WaitNode* newWaitNode = malloc(sizeof(WaitNode));
            cnd_init(&newWaitNode->cond);
            newWaitNode->thread_id = thrd_current();
            newWaitNode->next = NULL;
            queue.waitTail->next = newWaitNode;
            queue.waitTail = newWaitNode;
        }

        atomic_fetch_add(&queue.waiting, 1);
        cnd_wait(&queue.waitHead->cond, &queue.lock);
        atomic_fetch_sub(&queue.waiting, 1);

        if (queue.waitHead == queue.waitTail) {
            queue.waitHead = queue.waitTail = NULL;
        } else {
            WaitNode* temp = queue.waitHead;
            queue.waitHead = queue.waitHead->next;
            cnd_destroy(&temp->cond);
            free(temp);
        }
    }

    Node* node = queue.items.head;
    queue.items.head = node->next;
    if (!queue.items.head) {
        queue.items.tail = NULL;
    }

    void* item = node->data;
    free(node);

    atomic_fetch_add(&queue.visited, 1);

    if (queue.waitHead) {
        cnd_signal(&queue.waitHead->cond);
    }

    mtx_unlock(&queue.lock);
    return item;
}

// The rest of your functions (tryDequeue, size, waiting, visited) remain the same.

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

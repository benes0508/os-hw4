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

    if (queue.waitHead) {
        // If there are waiting threads, wake the first one and pass the item directly.
        Node* newNode = malloc(sizeof(Node));
        newNode->data = item;
        newNode->next = NULL;

        // Attach item directly to the waiting node for direct retrieval.
        queue.waitHead->data = newNode;
        cnd_signal(&queue.waitHead->cond);
    } else {
        // No waiting threads, just insert the item normally.
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

    while (!queue.items.head && !queue.waitHead) {
        // If there's no item and no direct hand-off, wait.
        WaitNode* waitNode = malloc(sizeof(WaitNode));
        cnd_init(&waitNode->cond);
        waitNode->thread_id = thrd_current();
        waitNode->next = NULL;
        waitNode->data = NULL;

        if (!queue.waitTail) {
            queue.waitHead = waitNode;
            queue.waitTail = waitNode;
        } else {
            queue.waitTail->next = waitNode;
            queue.waitTail = waitNode;
        }

        cnd_wait(&waitNode->cond, &queue.lock);

        // After waking up, check if there's direct data hand-off.
        if (waitNode->data) {
            Node* node = waitNode->data;
            void* item = node->data;
            free(node);
            free(waitNode);
            mtx_unlock(&queue.lock);
            return item;
        }
    }

    // Regular dequeue process if there's no waiting node or direct hand-off.
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

// The rest of your functions (tryDequeue, size, waiting, visited) remain the same.
bool tryDequeue(void** item) {
    mtx_lock(&queue.lock);

    // Adjusted to reference the queue.items.head instead of queue.head
    if (!queue.items.head) {
        mtx_unlock(&queue.lock);
        return false;
    }

    // Adjusted to reference the queue.items.head instead of queue.head
    Node* node = queue.items.head;
    queue.items.head = node->next;
    
    // Adjusted to reference the queue.items.head and queue.items.tail
    if (!queue.items.head) {
        queue.items.tail = NULL;
    }

    *item = node->data;
    free(node);
    
    // This line does not need to adjust because visited is not encapsulated.
    atomic_fetch_add(&queue.visited, 1);

    mtx_unlock(&queue.lock);
    return true;
}

size_t size(void) {
    // Adjusted to reference the correct item count
    size_t count = 0;
    mtx_lock(&queue.lock);
    Node* current = queue.items.head;
    while (current) {
        count++;
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

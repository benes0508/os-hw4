#include <stdatomic.h>
#include <threads.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

typedef struct packet {
    void* packet_data; 
    struct packet* next_packet; // next packet
} packet;

typedef struct sync_node {
    cnd_t w_sig; // cond var
    struct sync_node* link; // next in queue
} sync_node;

typedef struct {
    mtx_t stream_lock; //  exclusive access 
    cnd_t is_data_avail; // Signal that data is available.
    packet* entry_point; 
    packet* exit_point; 
    atomic_uint active_ticket; 
    atomic_uint next_ticket; 
    sync_node* first_idle; 
    sync_node* last_idle; 
    mtx_t sync_lock; 
    atomic_size_t size; 
    atomic_size_t waiting; 
    atomic_size_t visited; 
} big_queue;

big_queue queue;

void initQueue(void) {
    // some boring initilization code:
    queue.exit_point = NULL;
    queue.entry_point = NULL;

    mtx_init(&queue.stream_lock, mtx_plain);
    cnd_init(&queue.is_data_avail);

    atomic_store(&queue.visited, 0);
    atomic_store(&queue.size, 0);
    atomic_store(&queue.waiting, 0);
    queue.last_idle = NULL;
    queue.first_idle = NULL;

    mtx_init(&queue.sync_lock, mtx_plain);

    atomic_store(&queue.next_ticket, 0);
    atomic_store(&queue.active_ticket, 0);
}

void destroyQueue(void) {
    // do not hog the computers memory! #freeMemory
    packet* curr = queue.entry_point;
    while (curr) 
    {
        // while curr is not null, free the packets
        packet* rel = curr; // packet to release
        curr = curr->next_packet;
        free(rel);
    }

    sync_node* curr_sync_node = queue.first_idle;
    while (curr_sync_node) {
        sync_node* clean_node = curr_sync_node;
        curr_sync_node = curr_sync_node->link;
        cnd_destroy(&clean_node->w_sig);
        free(clean_node);
    }

    mtx_destroy(&queue.stream_lock);
    cnd_destroy(&queue.is_data_avail);
    mtx_destroy(&queue.sync_lock);
}

void enqueue(void* packet_data) {
    // add to queue
    packet* new_packet = malloc(sizeof(packet)); // Assuming malloc does not fail :)

    new_packet->packet_data = packet_data;
    new_packet->next_packet = NULL;
    mtx_lock(&queue.stream_lock);

    if (!queue.exit_point) 
    {
        queue.entry_point = queue.exit_point = new_packet;
        cnd_signal(&queue.is_data_avail);
    }
     else 
     {
        queue.exit_point->next_packet = new_packet;
        queue.exit_point = new_packet;
    }
    atomic_fetch_add(&queue.size, 1);
    mtx_unlock(&queue.stream_lock);
}

void* dequeue(void) {
    mtx_lock(&queue.stream_lock);
    while (!queue.entry_point) 
    {
        cnd_wait(&queue.is_data_avail, &queue.stream_lock);
    }
    packet* packet = queue.entry_point;
    queue.entry_point = packet->next_packet;
    if (!queue.entry_point) 
    {
        queue.exit_point = NULL;
    }
    void* packet_data = packet->packet_data;
    free(packet);
    atomic_fetch_sub(&queue.size, 1);
    atomic_fetch_add(&queue.visited, 1);
    if (queue.entry_point) 
    {
        cnd_signal(&queue.is_data_avail);
    }
    mtx_unlock(&queue.stream_lock);
    return packet_data;
}

bool tryDequeue(void** packet_data) {
    if (mtx_trylock(&queue.stream_lock) == thrd_success) 
    {
        if (!queue.entry_point) 
        {
            mtx_unlock(&queue.stream_lock);
            return false;
        }
        packet* packet = queue.entry_point;
        *packet_data = packet->packet_data;
        queue.entry_point = queue.entry_point->next_packet;
        if (!queue.entry_point) 
        { 
            queue.exit_point = NULL;
        }
        atomic_fetch_sub(&queue.size, 1);
        atomic_fetch_add(&queue.visited, 1);
        free(packet);
        mtx_unlock(&queue.stream_lock);
        return true;
    }
    return false;
}

// and now, for the easy part of the assignment:
size_t size(void) 
{
    return atomic_load(&queue.size);
}
size_t visited(void) 
{
    return atomic_load(&queue.visited);
}
size_t waiting(void) 
{
    return atomic_load(&queue.waiting);
}
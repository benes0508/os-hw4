#include <stdatomic.h>
#include <threads.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>



// we will use 2 queue method for the problematic ones
typedef struct Node {
    void* Meida; // Contents
    struct Node* habaBator; // Link to the next Thrd
} Node;

typedef struct threads 
{
    cnd_t waitingList; 
    struct threads* link; 
} threads;

// Core structure of the Data Stream Processor.
typedef struct 
{
    // Entry  and- the Exit point of the data stream.
    // Head of the waiting processors list.
    threads* firstIdle; 
    // Tail of the waiting processors list.
    threads* lastIdle; 
    Node* inlet; 
    Node* outlet; 
    atomic_uint activeTicket; 
    atomic_uint nextTicket; 
    mtx_t syncLock; // reminder --- (check what it is and delete this comment)
    atomic_size_t streamVolume; 
    atomic_size_t idleProcessors; 
    // Count of processors waiting for data.
    atomic_size_t processedThrds; 
        // Ensures exclusive access to the data stream.
    mtx_t streamLock; 
    // Signal that data is present in the stream.
    cnd_t dataAvailable; 
} CoreStructDS;

CoreStructDS dataQueue;

size_t size(void) {return atomic_load(&dataQueue.streamVolume);}
size_t waiting(void) {return atomic_load(&dataQueue.idleProcessors);}
size_t visited(void) {return atomic_load(&dataQueue.processedThrds);}

void initQueue(void)
 {
    dataQueue.outlet = NULL;
    dataQueue.inlet = NULL;
    
    
    // check mtx does
    mtx_init(&dataQueue.streamLock, mtx_plain);
    cnd_init(&dataQueue.dataAvailable);

    // atomic 
    atomic_store(&dataQueue.processedThrds, 0);
    atomic_store(&dataQueue.streamVolume, 0);
    atomic_store(&dataQueue.idleProcessors, 0);


    // set to null the first and last data Streaming :::::: 
    dataQueue.lastIdle = NULL;
    dataQueue.firstIdle = NULL;


    mtx_init(&dataQueue.syncLock, mtx_plain);

    //atomic
    atomic_store(&dataQueue.nextTicket, 0);
    atomic_store(&dataQueue.activeTicket, 0);
}

void destroyQueue(void) {
    // free all the memory, destory the queue and destroy mtx both things that needed. lock sync and all this
    Node* currentThrd = dataQueue.inlet;
    while (currentThrd) {
        Node* toRelease = currentThrd;
        currentThrd = currentThrd->habaBator;
        free(toRelease);
    }
    threads* currentSyncNode = dataQueue.firstIdle;
    while (currentSyncNode) {
        threads* toClean = currentSyncNode;
        currentSyncNode = currentSyncNode->link;
        cnd_destroy(&toClean->waitingList);
        free(toClean); // free !
    }
    cnd_destroy(&dataQueue.dataAvailable);

    mtx_destroy(&dataQueue.streamLock);
    
    mtx_destroy(&dataQueue.syncLock);
}

void enqueue(void* Meida) {
    // Introduces a new data Thrd into the stream.
    Node* newThrd = malloc(sizeof(Node));
    // setup
    newThrd->Meida = Meida;
    newThrd->habaBator = NULL;
    mtx_lock(&dataQueue.streamLock);
    if (!dataQueue.outlet) {
        dataQueue.inlet = dataQueue.outlet = newThrd;
        cnd_signal(&dataQueue.dataAvailable);
    } else {
        dataQueue.outlet->habaBator = newThrd;
        dataQueue.outlet = newThrd;
    }
    atomic_fetch_add(&dataQueue.streamVolume, 1);
    mtx_unlock(&dataQueue.streamLock);
}

void* dequeue(void) {
    // Extracts and processes a data Thrd from the stream.
    mtx_lock(&dataQueue.streamLock);
    while (!dataQueue.inlet) {cnd_wait(&dataQueue.dataAvailable, &dataQueue.streamLock);}
    Node* Thrd = dataQueue.inlet;
    dataQueue.inlet = Thrd->habaBator;
    if (!dataQueue.inlet) {dataQueue.outlet = NULL;}
    void* Meida = Thrd->Meida;
    // never forget to free!!!! segmentation was a pain 
    free(Thrd); // free it
    atomic_fetch_sub(&dataQueue.streamVolume, 1);
    atomic_fetch_add(&dataQueue.processedThrds, 1);
    if (dataQueue.inlet) {cnd_signal(&dataQueue.dataAvailable);}
    mtx_unlock(&dataQueue.streamLock);
    return Meida;
}
// try Dequeiue function implment : 
bool tryDequeue(void** Meida) {
    // lets try to process data without waiting for signal.
    if (mtx_trylock(&dataQueue.streamLock) == thrd_success) {
        if (!dataQueue.inlet) {
            mtx_unlock(&dataQueue.streamLock);
            return false;
        }
    // continue
        Node* baseNode = dataQueue.inlet;
        *Meida = baseNode->Meida;
        dataQueue.inlet = dataQueue.inlet->habaBator;
        if (!dataQueue.inlet) {dataQueue.outlet = NULL;}
        atomic_fetch_sub(&dataQueue.streamVolume, 1);
        atomic_fetch_add(&dataQueue.processedThrds, 1);
        // free base node ::
        free(baseNode);
        //mtx thig;
        mtx_unlock(&dataQueue.streamLock);
        return true;
    }
    return false;
}

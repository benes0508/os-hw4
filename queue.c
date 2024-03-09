#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

// Data Packet Node in the Stream.
typedef struct DataPacket {
    void* payload; // Contents of this packet.
    struct DataPacket* flowNext; // Link to the next packet in the stream.
} DataPacket;

// For synchronizing data flow processors awaiting tasks.
typedef struct ProcessorSyncNode {
    cnd_t awaitSignal; // Signal for processors to synchronize.
    struct ProcessorSyncNode* link; // Chain to the next node.
} ProcessorSyncNode;

// Core structure of the Data Stream Processor.
typedef struct {
    DataPacket* inlet; // Entry point of the data stream.
    DataPacket* outlet; // Exit point of the data stream.
    mtx_t streamLock; // Ensures exclusive access to the data stream.
    cnd_t dataAvailable; // Signal that data is present in the stream.
    atomic_size_t streamVolume; // Quantity of data packets in the stream.
    atomic_size_t idleProcessors; // Count of processors waiting for data.
    atomic_size_t processedPackets; // Tally of processed data packets.
    ProcessorSyncNode* firstIdle; // Head of the waiting processors list.
    ProcessorSyncNode* lastIdle; // Tail of the waiting processors list.
    mtx_t syncLock; // Lock for coordinating the idle processors.
    atomic_uint activeTicket; // Currently active processing ticket.
    atomic_uint nextTicket; // Ticket for the next processing sequence.
} DataStreamProcessor;

DataStreamProcessor dataStream;

void initQueue(void) {
    // Prepares the data stream for operation.
    dataStream.inlet = NULL;
    dataStream.outlet = NULL;
    mtx_init(&dataStream.streamLock, mtx_plain);
    cnd_init(&dataStream.dataAvailable);
    atomic_store(&dataStream.streamVolume, 0);
    atomic_store(&dataStream.idleProcessors, 0);
    atomic_store(&dataStream.processedPackets, 0);
    dataStream.firstIdle = NULL;
    dataStream.lastIdle = NULL;
    mtx_init(&dataStream.syncLock, mtx_plain);
    atomic_store(&dataStream.activeTicket, 0);
    atomic_store(&dataStream.nextTicket, 0);
}

void destroyQueue(void) {
    // Deactivates the data stream and cleans up.
    DataPacket* currentPacket = dataStream.inlet;
    while (currentPacket) {
        DataPacket* toRelease = currentPacket;
        currentPacket = currentPacket->flowNext;
        free(toRelease);
    }

    ProcessorSyncNode* currentSyncNode = dataStream.firstIdle;
    while (currentSyncNode) {
        ProcessorSyncNode* toClean = currentSyncNode;
        currentSyncNode = currentSyncNode->link;
        cnd_destroy(&toClean->awaitSignal);
        free(toClean);
    }

    mtx_destroy(&dataStream.streamLock);
    cnd_destroy(&dataStream.dataAvailable);
    mtx_destroy(&dataStream.syncLock);
}

void enqueue(void* payload) {
    // Introduces a new data packet into the stream.
    DataPacket* newPacket = malloc(sizeof(DataPacket));
    if (!newPacket) {
        fprintf(stderr, "Allocation error: Failed to create new data packet.\n");
        return;
    }
    newPacket->payload = payload;
    newPacket->flowNext = NULL;

    mtx_lock(&dataStream.streamLock);

    if (!dataStream.outlet) {
        dataStream.inlet = dataStream.outlet = newPacket;
        cnd_signal(&dataStream.dataAvailable);
    } else {
        dataStream.outlet->flowNext = newPacket;
        dataStream.outlet = newPacket;
    }

    atomic_fetch_add(&dataStream.streamVolume, 1);

    mtx_unlock(&dataStream.streamLock);
}

void* dequeue(void) {
    // Extracts and processes a data packet from the stream.
    mtx_lock(&dataStream.streamLock);

    while (!dataStream.inlet) {
        cnd_wait(&dataStream.dataAvailable, &dataStream.streamLock);
    }

    DataPacket* packet = dataStream.inlet;
    dataStream.inlet = packet->flowNext;
    if (!dataStream.inlet) {
        dataStream.outlet = NULL;
    }

    void* payload = packet->payload;
    free(packet);

    atomic_fetch_sub(&dataStream.streamVolume, 1);
    atomic_fetch_add(&dataStream.processedPackets, 1);

    if (dataStream.inlet) {
        cnd_signal(&dataStream.dataAvailable);
    }

    mtx_unlock(&dataStream.streamLock);
    return payload;
}

bool tryDequeue(void** payload) {
    // Tries to process data without waiting for signal.
    if (mtx_trylock(&dataStream.streamLock) == thrd_success) {
        if (!dataStream.inlet) {
            mtx_unlock(&dataStream.streamLock);
            return false;
        }

        DataPacket* packet = dataStream.inlet;
        *payload = packet->payload;
        dataStream.inlet = dataStream.inlet->flowNext;

        if (!dataStream.inlet) {
            dataStream.outlet = NULL;
        }

        atomic_fetch_sub(&dataStream.streamVolume, 1);
        atomic_fetch_add(&dataStream.processedPackets, 1);

        free(packet);
        mtx_unlock(&dataStream.streamLock);
        return true;
    }
    return false;
}

size_t size(void) {
    // Reports the current volume of data in the stream.
    return atomic_load(&dataStream.streamVolume);
}

size_t waiting(void) {
    // Lists the number of processors in idle state.
    return atomic_load(&dataStream.idleProcessors);
}

size_t visited(void) {
    // Counts the packets processed over time.
    return atomic_load(&dataStream.processedPackets);
}

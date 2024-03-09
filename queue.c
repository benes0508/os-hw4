#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

// A structure representing each passenger in the line.
typedef struct PassengerNode {
    void* luggage; // Luggage carried by the passenger.
    struct PassengerNode* nextInLine; // Pointer to the next passenger in line.
} PassengerNode;

// A structure for managing passengers waiting for their boarding call.
typedef struct BoardingCallNode {
    cnd_t boardingCall; // Condition variable for boarding call.
    struct BoardingCallNode* nextWaitingPassenger; // Pointer to the next passenger waiting for boarding call.
} BoardingCallNode;

// The primary structure for the passenger queue at the boarding gate.
typedef struct {
    PassengerNode* boardingGateStart; // Pointer to the first passenger in line.
    PassengerNode* boardingGateEnd; // Pointer to the last passenger in line.
    mtx_t boardingGateMutex; // Mutex for synchronizing access to the passenger line.
    cnd_t boardingQueueNotEmpty; // Condition variable to signal when the line is not empty.
    atomic_size_t queueLength; // The total number of passengers in line.
    atomic_size_t waitingPassengers; // Number of passengers waiting for boarding to start.
    atomic_size_t processedPassengers; // Count of passengers that have been processed for boarding.
    BoardingCallNode* firstWaitingPassenger; // Pointer to the first passenger in the waiting area.
    BoardingCallNode* lastWaitingPassenger; // Pointer to the last passenger in the waiting area.
    mtx_t waitingAreaMutex; // Mutex for synchronizing access to the waiting area.
    atomic_uint currentBoardingPass; // The current boarding pass being processed.
    atomic_uint nextBoardingPass; // The next boarding pass to be issued.
} BoardingQueue;


BoardingQueue boardingQueue;

void initializeBoardingQueue(void) {
    // Prepare the boarding queue before flights start boarding.
    boardingQueue.boardingGateStart = NULL;
    boardingQueue.boardingGateEnd = NULL;
    mtx_init(&boardingQueue.boardingGateMutex, mtx_plain);
    cnd_init(&boardingQueue.boardingQueueNotEmpty);
    atomic_store(&boardingQueue.queueLength, 0);
    atomic_store(&boardingQueue.waitingPassengers, 0);
    atomic_store(&boardingQueue.processedPassengers, 0);
    boardingQueue.firstWaitingPassenger = NULL;
    boardingQueue.lastWaitingPassenger = NULL;
    mtx_init(&boardingQueue.waitingAreaMutex, mtx_plain);
    atomic_store(&boardingQueue.currentBoardingPass, 0);
    atomic_store(&boardingQueue.nextBoardingPass, 0);
}

void clearBoardingQueue(void) {
    // Tidy up after the last flight has boarded.
    PassengerNode* currentPassenger = boardingQueue.boardingGateStart;
    while (currentPassenger) {
        PassengerNode* temp = currentPassenger;
        currentPassenger = currentPassenger->nextInLine;
        free(temp); // Free memory allocated for each passenger node.
    }
    
    BoardingCallNode* currentCallNode = boardingQueue.firstWaitingPassenger;
    while (currentCallNode) {
        BoardingCallNode* temp = currentCallNode;
        currentCallNode = currentCallNode->nextWaitingPassenger;
        cnd_destroy(&temp->boardingCall); // Clean up each waiting passenger node.
        free(temp);
    }

    // Dispose of synchronization primitives.
    mtx_destroy(&boardingQueue.boardingGateMutex);
    cnd_destroy(&boardingQueue.boardingQueueNotEmpty);
    mtx_destroy(&boardingQueue.waitingAreaMutex);
}

void boardPassenger(void* luggage) {
    // Enqueue a passenger with their luggage at the end of the boarding line.
    PassengerNode* newPassenger = malloc(sizeof(PassengerNode));
    if (!newPassenger) {
        fprintf(stderr, "Error: Unable to allocate memory for new passenger.\n");
        return;
    }
    newPassenger->luggage = luggage;
    newPassenger->nextInLine = NULL;

    mtx_lock(&boardingQueue.boardingGateMutex);

    if (boardingQueue.boardingGateEnd == NULL) {
        boardingQueue.boardingGateStart = boardingQueue.boardingGateEnd = newPassenger;
        cnd_signal(&boardingQueue.boardingQueueNotEmpty);
    } else {
        boardingQueue.boardingGateEnd->nextInLine = newPassenger;
        boardingQueue.boardingGateEnd = newPassenger;
    }

    atomic_fetch_add(&boardingQueue.queueLength, 1);

    mtx_unlock(&boardingQueue.boardingGateMutex);
}

void* deboardPassenger(void) {
    // Dequeue a passenger from the start of the boarding line.
    mtx_lock(&boardingQueue.boardingGateMutex);

    while (boardingQueue.boardingGateStart == NULL) {
        cnd_wait(&boardingQueue.boardingQueueNotEmpty, &boardingQueue.boardingGateMutex);
    }

    PassengerNode* passenger = boardingQueue.boardingGateStart;
    boardingQueue.boardingGateStart = passenger->nextInLine;
    if (boardingQueue.boardingGateStart == NULL) {
        boardingQueue.boardingGateEnd = NULL;
    }

    void* luggage = passenger->luggage;
    free(passenger);

    atomic_fetch_sub(&boardingQueue.queueLength, 1);
    atomic_fetch_add(&boardingQueue.processedPassengers, 1);

    if (boardingQueue.boardingGateStart != NULL) {
        cnd_signal(&boardingQueue.boardingQueueNotEmpty);
    }

    mtx_unlock(&boardingQueue.boardingGateMutex);
    return luggage;
}

bool tryBoardPassenger(void** luggage) {
    // Attempt to board a passenger without delay.
    if (mtx_trylock(&boardingQueue.boardingGateMutex) == thrd_success) {
        if (boardingQueue.boardingGateStart == NULL) {
            mtx_unlock(&boardingQueue.boardingGateMutex);
            return false;
        }

        PassengerNode* tempPassenger = boardingQueue.boardingGateStart;
        *luggage = tempPassenger->luggage;
        boardingQueue.boardingGateStart = boardingQueue.boardingGateStart->nextInLine;
        
        if (boardingQueue.boardingGateStart == NULL) {
            boardingQueue.boardingGateEnd = NULL;
        }

        atomic_fetch_sub(&boardingQueue.queueLength, 1);
        atomic_fetch_add(&boardingQueue.processedPassengers, 1);

        free(tempPassenger);
        mtx_unlock(&boardingQueue.boardingGateMutex);

        return true; // Successfully boarded a passenger.
    }

    return false; // Boarding gate was busy, try again later.
}

size_t passengersWaiting(void) {
    // Check how many passengers are currently waiting in line.
    return atomic_load(&boardingQueue.queueLength);
}

size_t passengersProcessed(void) {
    // Count how many passengers have successfully boarded.
    return atomic_load(&boardingQueue.processedPassengers);
}

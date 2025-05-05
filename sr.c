#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

#define WINDOW_SIZE 6
#define SEQ_SPACE 12  // Must be at least 2*WINDOW_SIZE for SR to work correctly
#define NOTINUSE (-1)
#define RTT 16.0       // Round trip time (adjust as needed, but emulator example uses 16.0)

/* Sender (A) variables */
static struct pkt sender_buffer[SEQ_SPACE];  // Buffer for packets sent but not yet acked
static bool acked[SEQ_SPACE];                // Track if specific seqnum is acknowledged
static int sender_base;                      // Sequence number of the oldest unacked packet
static int next_seqnum;                      // Next sequence number to use for a new packet
static bool A_timer_is_active;               // Flag to track if the single timer for A is running

/* Receiver (B) variables */
static struct pkt receiver_buffer[SEQ_SPACE]; // Buffer for out-of-order packets
static bool received[SEQ_SPACE];              // Track received packets at receiver
static int receiver_base;                     // Sequence number of the expected packet at receiver

/* Compute checksum for packet integrity verification */
int ComputeChecksum(struct pkt packet)
{
    int checksum = 0;
    int i;

    checksum = packet.seqnum;
    checksum += packet.acknum;
    for ( i=0; i<20; i++ ) {
        checksum += (int)(packet.payload[i]);
    }

    return checksum;
}

/* Check if a packet is corrupted by verifying its checksum */
bool IsCorrupted(struct pkt packet)
{
    return packet.checksum != ComputeChecksum(packet);
}


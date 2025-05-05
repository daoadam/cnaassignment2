#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

#define WINDOW_SIZE 6
#define SEQ_SPACE 12  // needs to be at least 2×window size to prevent seq number overlap
#define NOTINUSE (-1)
#define RTT 16.0       // simulated round-trip time (adjust if needed)

/* ---------- sender-side state ---------- */
static struct pkt sender_buffer[SEQ_SPACE];  // stores unacked packets
static bool acked[SEQ_SPACE];                // tracks which packets have been acked
static int sender_base;                      // oldest unacked seq number
static int next_seqnum;                      // next sequence number to use
static bool A_timer_is_active;               // true if timer is running

/* ---------- receiver-side state ---------- */
static struct pkt receiver_buffer[SEQ_SPACE];  // holds out-of-order packets
static bool received[SEQ_SPACE];               // marks which packets were reccived
static int receiver_base;                      // expected next seq number

/* ---------- utility functions ---------- */

// computes a simple checksum
int ComputeChecksum(struct pkt packet) {
    int checksum = packet.seqnum + packet.acknum;
    for (int i = 0; i < 20; i++) {
        checksum += (int)(packet.payload[i]);
    }
    return checksum;
}

// checks if a packet is corrupted
bool IsCorrupted(struct pkt packet) {
    return packet.checksum != ComputeChecksum(packet);
}

// checks if seq number is in the window (handles wrap-around)
bool IsInWindow(int seq, int base, int window_size) {
    int end = (base + window_size) % SEQ_SPACE;
    if (base < end) {
        return seq >= base && seq < end;
    } else {
        return seq >= base || seq < end;
    }
}

/* called by layer 5 to send data from app to sender */
void A_output(struct msg message)
{
    int in_flight = (next_seqnum - sender_base + SEQ_SPACE) % SEQ_SPACE;

    // drop if window is full
    if (in_flight >= WINDOW_SIZE) {
        if (TRACE > 0) {
            printf("----a: window full, dropping message\n");
        }
        window_full++;
        return;
    }

    // build packet from message
    struct pkt packet;
    packet.seqnum = next_seqnum;
    packet.acknum = NOTINUSE;

    for (int i = 0; i < 20; i++) {
        packet.payload[i] = message.data[i];
    }

    packet.checksum = ComputeChecksum(packet);
    sender_buffer[next_seqnum] = packet;
    acked[next_seqnum] = false;

    // send packet to network
    if (TRACE > 0) {
        printf("----a: sending packet %d\n", next_seqnum);
    }
    tolayer3(A, packet);

    // start timer if first packet in window
    if (sender_base == next_seqnum && !A_timer_is_active) {
        if (TRACE > 1) printf("----a: starting timer\n");
        starttimer(A, RTT);
        A_timer_is_active = true;
    }

    next_seqnum = (next_seqnum + 1) % SEQ_SPACE;
}
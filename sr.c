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

/* handles incoming acks at sender */
void A_input(struct pkt packet)
{
    if (IsCorrupted(packet)) {
        if (TRACE > 0) printf("----a: corrupted ack, ignored\n");
        return;
    }

    if (IsInWindow(packet.acknum, sender_base, WINDOW_SIZE)) {
        total_ACKs_received++;

        if (!acked[packet.acknum]) {
            if (TRACE > 0) printf("----a: ack %d received\n", packet.acknum);
            acked[packet.acknum] = true;
            new_ACKs++;

            // move base if this was for the base packet
            if (packet.acknum == sender_base) {
                if (TRACE > 1) printf("----a: sliding window\n");

                if (A_timer_is_active) {
                    stoptimer(A);
                    A_timer_is_active = false;
                }

                while (acked[sender_base]) {
                    acked[sender_base] = false;
                    sender_base = (sender_base + 1) % SEQ_SPACE;
                    if (TRACE > 1) printf("----a: moved base to %d\n", sender_base);
                }

                if (sender_base != next_seqnum) {
                    if (TRACE > 1) printf("----a: restarting timer\n");
                    starttimer(A, RTT);
                    A_timer_is_active = true;
                }
            } else {
                if (TRACE > 1) printf("----a: mid-window ack, timer continues\n");
            }
        } else {
            if (TRACE > 0) printf("----a: duplicate ack %d, skipping\n", packet.acknum);
        }
    } else {
        if (TRACE > 0) printf("----a: ack %d not in window, ignoring\n", packet.acknum);
    }
}
/* called when sender's timer fires */
void A_timerinterrupt(void)
{
    A_timer_is_active = false;

    if (sender_base == next_seqnum) {
        if (TRACE > 0) printf("----a: timer went off but nothing to resend\n");
        return;
    }

    if (TRACE > 0) printf("----a: timeout, resending packet %d\n", sender_base);
    tolayer3(A, sender_buffer[sender_base]);
    packets_resent++;

    starttimer(A, RTT);
    A_timer_is_active = true;
}

/* sets up the sender at sim start */
void A_init(void)
{
    sender_base = 0;
    next_seqnum = 0;
    A_timer_is_active = false;

    for (int i = 0; i < SEQ_SPACE; i++) {
        acked[i] = false;
    }

    if (TRACE > 0) {
        printf("----a: init done\n");
    }
}

/* ---------------- receiver side ---------------- */

void B_input(struct pkt packet)
{
    if (IsCorrupted(packet)) {
        if (TRACE > 0) printf("----b: corrupted packet %d, dropping\n", packet.seqnum);
        return;
    }

    if (TRACE > 1) printf("----b: packet %d received (expect %d)\n", packet.seqnum, receiver_base);

    struct pkt ack_packet;
    ack_packet.seqnum = NOTINUSE;
    ack_packet.acknum = packet.seqnum;
    for (int i = 0; i < 20; i++) ack_packet.payload[i] = '0';
    ack_packet.checksum = ComputeChecksum(ack_packet);

    bool in_window = IsInWindow(packet.seqnum, receiver_base, WINDOW_SIZE);
    bool ok_for_ack = IsInWindow(packet.seqnum, (receiver_base - WINDOW_SIZE + SEQ_SPACE) % SEQ_SPACE, 2 * WINDOW_SIZE);

    if (ok_for_ack) {
        if (TRACE > 0) printf("----b: sending ack %d\n", packet.seqnum);
        tolayer3(B, ack_packet);
    } else {
        if (TRACE > 0) printf("----b: packet %d is too old, not acking\n", packet.seqnum);
        return;
    }

    if (in_window) {
        if (!received[packet.seqnum]) {
            if (TRACE > 1) printf("----b: new packet %d in window, buffeer it\n", packet.seqnum);
            receiver_buffer[packet.seqnum] = packet;
            received[packet.seqnum] = true;

            if (packet.seqnum == receiver_base) {
                if (TRACE > 1) printf("----b: delivering in-order packets\n");

                while (received[receiver_base]) {
                    if (TRACE > 0) printf("----b: delivering %d to app\n", receiver_base);
                    tolayer5(B, receiver_buffer[receiver_base].payload);
                    packets_received++;
                    received[receiver_base] = false;
                    receiver_base = (receiver_base + 1) % SEQ_SPACE;
                    if (TRACE > 1) printf("----b: receiver base now %d\n", receiver_base);
                }
            }
        } else {
            if (TRACE > 1) printf("----b: duplicate packet %d, already buffered\n", packet.seqnum);
        }
    } else {
        if (TRACE > 1) printf("----b: packet %d not in window, ignoring\n", packet.seqnum);
    }
}

void B_init(void)
{
    receiver_base = 0;

    for (int i = 0; i < SEQ_SPACE; i++) {
        received[i] = false;
    }

    if (TRACE > 0) {
        printf("----b: receiver initialized\n");
    }
}

void B_output(struct msg message) {
    if (TRACE > 0) printf("----b: b_output not implemented lol\n");
}

void B_timerinterrupt(void) {
    if (TRACE > 0) printf("----b: no timer for b anyway\n");
}

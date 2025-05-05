#ifndef SR_H
#define SR_H

#include "emulator.h"

/* Sender functions */
void A_output(struct msg message);    /* Called from layer 5 to send a message */
void A_input(struct pkt packet);      /* Called from layer 3 when a packet arrives for layer 4 at A */
void A_timerinterrupt(void);          /* Called when A's timer goes off */
void A_init(void);                    /* Initialize sender A */

/* Receiver functions */
void B_input(struct pkt packet);      /* Called from layer 3 when a packet arrives for layer 4 at B */
void B_output(struct msg message);    /* Dummy function for bidirectional support */
void B_timerinterrupt(void);          /* Dummy function for bidirectional support */
void B_init(void);                    /* Initialize receiver B */

/* Helper functions */
int ComputeChecksum(struct pkt packet);  /* Calculate packet checksum */
bool IsCorrupted(struct pkt packet);     /* Check if packet is corrupted */
bool IsInWindow(int seq, int base, int windowsize); /* Check if sequence number is within window */

#endif /* SR_H */
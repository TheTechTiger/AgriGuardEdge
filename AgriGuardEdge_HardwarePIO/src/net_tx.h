/** WiFi TX scheduler: connect → send → disconnect. Radio off between. */
#pragma once
#include <stdint.h>

void tx_init(void);
// Queue a payload (drops oldest on overflow, counts suppression).
// Returns false if the message was dropped.
bool tx_queue(const char *kind, const char *json);
// Flush the queue now. Returns messages sent.
int  tx_flush(void);
uint32_t tx_sent_total(void);
uint32_t tx_suppressed_total(void);

#pragma once
#include <furi.h>
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>

// Kita definisikan struktur untuk ISO 14443-4A (Pengganti IsoDep)
typedef struct Iso14443_4aPoller Iso14443_4aPoller;

typedef enum {
    Iso14443_4aPollerEventTypeReady,
    Iso14443_4aPollerEventTypeError,
} Iso14443_4aPollerEventType;

typedef struct {
    Iso14443_4aPollerEventType type;
    NfcError error;
    Iso14443_4aPoller* instance;
    void* data; // Padding
} Iso14443_4aPollerEvent;

// Fungsi Transceive untuk 4A (Kita deklarasikan manual)
NfcError iso14443_4a_poller_send_frame(
    Iso14443_4aPoller* instance,
    BitBuffer* tx_buffer,
    BitBuffer* rx_buffer,
    uint32_t fwt // Frame Waiting Time
);
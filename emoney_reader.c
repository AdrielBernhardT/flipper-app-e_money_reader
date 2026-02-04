#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a_poller.h>

#include "bank_data.h" 

#define AUTO_CLOSE_MS 5000

// --- EVENT TYPE ---
typedef enum {
    EventTypeInput,      // Event Button Press
    EventTypeNfcSuccess, // Event Card Detected & Balance Read
} EventType;

// --- APP STATE ---
typedef enum {
    AppStateScanning, // Waiting for Card
    AppStateSuccess, // Show Balance
} AppState;

// --- MAIN DATA ---
typedef struct {
    AppState state;
    uint32_t balance;
    char bank_name[32];
    uint32_t timer_start_tick;
    Nfc* nfc;
    NfcPoller* poller;
    bool processing;
} AppData;

// --- INTER-THREAD MESSAGE STRUCTURE ---
typedef struct {
    EventType type;
    InputEvent input;       // if type == EventTypeInput
    uint32_t balance_data;    // if type == EventTypeNfcSuccess
    char bank_name_data[32];
} AppEvent;

// === 1. NFC LOGIC ===
static bool send_apdu(Iso14443_4aPoller* iso4, const uint8_t* cmd, uint8_t cmd_len, BitBuffer* response) {
    BitBuffer* tx_buffer = bit_buffer_alloc(cmd_len);
    bit_buffer_append_bytes(tx_buffer, cmd, cmd_len);
    NfcError error = iso14443_4a_poller_send_frame(iso4, tx_buffer, response, 100000); // 100ms timeout
    bit_buffer_free(tx_buffer);
    return (error == NfcErrorNone);
}

static NfcCommand nfc_callback(NfcGenericEvent event, void* context) {
    FuriMessageQueue* queue = (FuriMessageQueue*)context;
    
    if(event.protocol == NfcProtocolIso14443_4a && event.event == NfcEventCardDetected) {
        Iso14443_4aPoller* iso4 = (Iso14443_4aPoller*)event.instance;
        BitBuffer* rx_buffer = bit_buffer_alloc(256);
        bool found = false;

        // Check every bank profile
        for(int i = 0; i < BANK_COUNT; i++) {
            bit_buffer_reset(rx_buffer);
            
            // A. Select AID
            uint8_t select_cmd[32] = {0x00, 0xA4, 0x04, 0x00};
            select_cmd[4] = BANK_DB[i].aid_len;
            memcpy(&select_cmd[5], BANK_DB[i].aid, BANK_DB[i].aid_len);
            
            if(send_apdu(iso4, select_cmd, 5 + BANK_DB[i].aid_len, rx_buffer)) {
                size_t rx_len = bit_buffer_get_size_bytes(rx_buffer);
                uint8_t* rx_data = bit_buffer_get_data(rx_buffer);
                
                // Check Response 90 00
                if(rx_len >= 2 && rx_data[rx_len-2] == 0x90 && rx_data[rx_len-1] == 0x00) {
                    
                    // B. Get Balance
                    bit_buffer_reset(rx_buffer);
                    if(send_apdu(iso4, BANK_DB[i].cmd_bal, BANK_DB[i].cmd_len, rx_buffer)) {
                        rx_len = bit_buffer_get_size_bytes(rx_buffer);
                        rx_data = bit_buffer_get_data(rx_buffer);
                        
                        if(rx_len >= 4) {
                            // C. Parsing Data
                            uint32_t balance_raw = 0;
                            uint8_t* target = rx_data + BANK_DB[i].offset;

                            if(BANK_DB[i].is_little_endian) {
                                balance_raw = target[0] | (target[1] << 8) | (target[2] << 16) | (target[3] << 24);
                            } else {
                                balance_raw = (target[0] << 24) | (target[1] << 16) | (target[2] << 8) | target_data[3];
                            }

                            // D. Send Success to Main Loop
                            AppEvent event_success;
                            event_success.type = EventTypeNfcSuccess;
                            event_success.balance_data = balance_raw;
                            strncpy(event_success.bank_name_data, BANK_DB[i].name, 31);
                            
                            furi_message_queue_put(queue, &event_success, 0);
                            found = true;
                            break; 
                        }
                    }
                }
            }
        }
        bit_buffer_free(rx_buffer);
        if (found) return NfcCommandStop; // Stop polling if found
    }
    return NfcCommandContinue;
}
// === 2. INPUT & UI LOGIC ===
// Input Callback: Make Back button responsive
static void input_callback(InputEvent* input_event, void* context) {
    FuriMessageQueue* event_queue = (FuriMessageQueue*)context;
    AppEvent event = {.type = EventTypeInput, .input = *input_event};
    furi_message_queue_put(event_queue, &event, 0);
}

static void draw_callback(Canvas* canvas, void* context) {
    AppData* data = (AppData*)context;
    canvas_clear(canvas);
    
    // Header
    canvas_draw_line(canvas, 0, 12, 128, 12);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, "Indo Reader");

    if(data->state == AppStateScanning) {
        // Scanning Display
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, "Scanning...");
        
        // Card Icon Animation (Blinking Box)
        if((furi_get_tick() % 1000) > 500) {
            canvas_draw_rframe(canvas, 45, 45, 38, 24, 2); 
            canvas_draw_str_aligned(canvas, 64, 57, AlignCenter, AlignCenter, "NFC");
        }
    } 
    else if(data->state == AppStateSuccess) {
        // Balance Display
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignCenter, data->bank_name);
        
        canvas_set_font(canvas, FontBigNumbers);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "Rp %ld", data->balance);
        canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignCenter, buffer);

        // Progress Bar Timer
        uint32_t elapsed = furi_get_tick() - data->timer_start_tick;
        if (elapsed > AUTO_CLOSE_MS) elapsed = AUTO_CLOSE_MS;
        float progress = 1.0f - ((float)elapsed / AUTO_CLOSE_MS);
        
        canvas_draw_frame(canvas, 10, 60, 108, 3);
        canvas_draw_box(canvas, 10, 60, (int)(108 * progress), 3);
    }
}

// === 3. MAIN PROGRAM LOOP ===

int32_t emoney_reader_app(void* p) {
    UNUSED(p);

    // Setup Queue & Data
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(AppEvent));
    AppData* data = malloc(sizeof(AppData));
    data->state = AppStateScanning;
    data->processing = true;

    // Setup NFC
    data->nfc = nfc_alloc();
    data->poller = nfc_poller_alloc(data->nfc, NfcProtocolIso14443_4a);
    nfc_poller_start(data->poller, nfc_callback, event_queue);

    // Setup View & GUI
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, data);
    view_port_input_callback_set(view_port, input_callback, event_queue); // Connect buttons to system

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);

    AppEvent event;
    
    while(data->processing) { // while app is running
        FuriStatus status = furi_message_queue_get(event_queue, &event, 100); // app wait for event

        if(status == FuriStatusOk) { // if event received
            // --- Case 1: BUTTON PRESSED ---
            if(event.type == EventTypeInput) { // Scanning Input Event
                if(event.input.key == InputKeyBack && event.input.type == InputTypeShort) { // Back Button pressed
                    
                    if(data->state == AppStateScanning) { // If Scanning -> Press Back -> EXIT APP
                        data->processing = false;
                    } 
                    else if(data->state == AppStateSuccess) { // If Viewing Balance -> Press Back -> RESET TO SCANNING
                        data->state = AppStateScanning;
                        nfc_poller_stop(data->poller);
                        nfc_poller_start(data->poller, nfc_callback, event_queue);
                    }
                }
            }
            // --- Case 2: CARD DETECTED ---
            else if(event.type == EventTypeNfcSuccess) { // Card Detected
                data->balance = event.balance_data; // Get Balance
                strncpy(data->bank_name, event.bank_name_data, 31); // Get Bank Name
                
                data->state = AppStateSuccess; // Switch to Success State
                data->timer_start_tick = furi_get_tick(); // Start 5-second timer
                notification_message(notifications, &sequence_success); // Show Success Notification
            }
        }

        // --- TIMER LOGIC (AUTO CLOSE/RESET) ---
        if(data->state == AppStateSuccess) { // If in Success State
            if(furi_get_tick() - data->timer_start_tick > AUTO_CLOSE_MS) { // 5 seconds passed? Return to Scanning
                data->state = AppStateScanning;
                nfc_poller_stop(data->poller);
                nfc_poller_start(data->poller, nfc_callback, event_queue);
            }
        }

        view_port_update(view_port);
    }

    // --- CLEANUP ---
    nfc_poller_stop(data->poller);
    nfc_poller_free(data->poller);
    nfc_free(data->nfc);

    furi_record_close(RECORD_NOTIFICATION);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(event_queue);
    free(data);

    return 0;
}
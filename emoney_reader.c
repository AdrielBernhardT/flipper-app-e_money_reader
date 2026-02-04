#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

// NFC Libraries
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a_poller.h>

#include "bank_data.h" 

#define AUTO_CLOSE_MS 5000 
#define EVENT_NFC_SUCCESS 100 

// Tambahkan definisi notifikasi ini
const NotificationSequence sequence_success = {
    &message_display_backlight_on,
    &message_green_255,
    &message_vibro_on,
    &message_note_c5,
    &message_delay_50,
    &message_vibro_off,
    &message_note_e5,
    &message_delay_50,
    &message_note_g5,
    &message_delay_50,
    &message_blink_stop,
    NULL,
};

// --- STATE APLIKASI ---
typedef enum {
    AppStateScanning,
    AppStateSuccess,
} AppState;

// --- EVENT ---
typedef struct {
    uint8_t type; 
    InputEvent input;
    uint32_t saldo_data;
    char bank_name_data[32];
} AppEvent;

// --- DATA UTAMA ---
typedef struct {
    AppState state;
    uint32_t saldo;
    char bank_name[32];
    uint32_t timer_start_tick;
    Nfc* nfc;
    NfcPoller* poller;
    FuriMessageQueue* event_queue; // Kita simpan Queue disini supaya callback bisa akses
    bool processing; 
} AppData;

// =============================================================================
// 1. NFC LOGIC (Menggunakan Public API nfc_poller_trx)
// =============================================================================

// Helper baru: Pakai Nfc* instance, bukan Iso14443_4aPoller*
static bool send_apdu(Nfc* nfc_instance, const uint8_t* cmd, uint8_t cmd_len, BitBuffer* response) {
    BitBuffer* tx_buffer = bit_buffer_alloc(cmd_len);
    bit_buffer_append_bytes(tx_buffer, cmd, cmd_len);
    
    // GUNAKAN FUNGSI RESMI INI (Ada di nfc_poller.h)
    // Timeout 100ms (dalam cycle, approx value untuk trx)
    NfcError error = nfc_poller_trx(nfc_instance, tx_buffer, response, 100000); 
    
    bit_buffer_free(tx_buffer);
    return (error == NfcErrorNone);
}

static NfcCommand nfc_callback(NfcGenericEvent event, void* context) {
    // Context sekarang adalah AppData*, bukan Queue langsung
    AppData* data = (AppData*)context;
    
    // 1. Cek Protokol
    if(event.protocol == NfcProtocolIso14443_4a) {
        
        // 2. CASTING MANUAL (Sesuai SDK Dev Kamu)
        Iso14443_4aPollerEvent* iso4_event = (Iso14443_4aPollerEvent*)event.event_data;
        
        // 3. Cek Status Ready
        if(iso4_event->type == Iso14443_4aPollerEventTypeReady) {
            
            BitBuffer* rx_buffer = bit_buffer_alloc(256);
            bool found = false;

            for(int i = 0; i < BANK_COUNT; i++) {
                bit_buffer_reset(rx_buffer);
                
                // A. Select AID
                uint8_t select_cmd[32] = {0x00, 0xA4, 0x04, 0x00};
                select_cmd[4] = BANK_DB[i].aid_len;
                memcpy(&select_cmd[5], BANK_DB[i].aid, BANK_DB[i].aid_len);
                
                // PENTING: Kita kirim pakai data->nfc (Instance Induk)
                if(send_apdu(data->nfc, select_cmd, 5 + BANK_DB[i].aid_len, rx_buffer)) {
                    size_t rx_len = bit_buffer_get_size_bytes(rx_buffer);
                    const uint8_t* rx_data = bit_buffer_get_data(rx_buffer);
                    
                    if(rx_len >= 2 && rx_data[rx_len-2] == 0x90 && rx_data[rx_len-1] == 0x00) {
                        
                        // B. Get Balance
                        bit_buffer_reset(rx_buffer);
                        if(send_apdu(data->nfc, BANK_DB[i].cmd_bal, BANK_DB[i].cmd_len, rx_buffer)) {
                            rx_len = bit_buffer_get_size_bytes(rx_buffer);
                            rx_data = bit_buffer_get_data(rx_buffer);
                            
                            if(rx_len >= 4) {
                                uint32_t saldo_raw = 0;
                                const uint8_t* target = rx_data + BANK_DB[i].offset;

                                if(BANK_DB[i].is_little_endian) {
                                    saldo_raw = target[0] | (target[1] << 8) | (target[2] << 16) | (target[3] << 24);
                                } else {
                                    saldo_raw = (target[0] << 24) | (target[1] << 16) | (target[2] << 8) | target[3];
                                }

                                AppEvent event_sukses;
                                event_sukses.type = EVENT_NFC_SUCCESS;
                                event_sukses.saldo_data = saldo_raw;
                                strncpy(event_sukses.bank_name_data, BANK_DB[i].name, 31);
                                
                                // Kirim ke Queue lewat data->event_queue
                                furi_message_queue_put(data->event_queue, &event_sukses, 0);
                                found = true;
                                break; 
                            }
                        }
                    }
                }
            }
            bit_buffer_free(rx_buffer);
            if (found) return NfcCommandStop; 
        }
    }
    return NfcCommandContinue;
}

// =============================================================================
// 2. UI & INPUT
// =============================================================================

static void input_callback(InputEvent* input_event, void* context) {
    FuriMessageQueue* event_queue = (FuriMessageQueue*)context;
    AppEvent event = {.type = 0, .input = *input_event};
    furi_message_queue_put(event_queue, &event, 0);
}

static void draw_callback(Canvas* canvas, void* context) {
    AppData* data = (AppData*)context;
    canvas_clear(canvas);
    
    canvas_draw_line(canvas, 0, 12, 128, 12);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, "Indo Reader");

    if(data->state == AppStateScanning) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, "Scanning...");
        
        if((furi_get_tick() % 1000) > 500) {
            canvas_draw_rframe(canvas, 45, 45, 38, 24, 2); 
            canvas_draw_str_aligned(canvas, 64, 57, AlignCenter, AlignCenter, "NFC");
        }
    } 
    else if(data->state == AppStateSuccess) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignCenter, data->bank_name);
        
        canvas_set_font(canvas, FontBigNumbers);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "Rp %ld", data->saldo);
        canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignCenter, buffer);

        uint32_t elapsed = furi_get_tick() - data->timer_start_tick;
        if (elapsed > AUTO_CLOSE_MS) elapsed = AUTO_CLOSE_MS;
        float progress = 1.0f - ((float)elapsed / AUTO_CLOSE_MS);
        
        canvas_draw_frame(canvas, 10, 60, 108, 3);
        canvas_draw_box(canvas, 10, 60, (int)(108 * progress), 3);
    }
}

// =============================================================================
// 3. MAIN LOOP
// =============================================================================

int32_t emoney_reader_app(void* p) {
    UNUSED(p);

    // 1. Setup Data & Queue
    AppData* data = malloc(sizeof(AppData));
    data->event_queue = furi_message_queue_alloc(8, sizeof(AppEvent)); // Simpan di struct
    data->state = AppStateScanning;
    data->processing = true;

    // 2. Setup NFC
    data->nfc = nfc_alloc();
    data->poller = nfc_poller_alloc(data->nfc, NfcProtocolIso14443_4a);
    
    // PENTING: Context callback sekarang adalah 'data', bukan 'event_queue'
    // Ini agar callback bisa akses 'data->nfc' untuk fungsi nfc_poller_trx
    nfc_poller_start(data->poller, nfc_callback, data);

    // 3. Setup UI
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, data);
    view_port_input_callback_set(view_port, input_callback, data->event_queue); 

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);

    AppEvent event;
    
    while(data->processing) {
        FuriStatus status = furi_message_queue_get(data->event_queue, &event, 100);

        if(status == FuriStatusOk) {
            if(event.type == 0) {
                if(event.input.key == InputKeyBack && event.input.type == InputTypeShort) {
                    if(data->state == AppStateScanning) {
                        data->processing = false;
                    } 
                    else if(data->state == AppStateSuccess) {
                        data->state = AppStateScanning;
                        nfc_poller_stop(data->poller); 
                        nfc_poller_start(data->poller, nfc_callback, data);
                    }
                }
            }
            else if(event.type == EVENT_NFC_SUCCESS) {
                data->saldo = event.saldo_data;
                strncpy(data->bank_name, event.bank_name_data, 31);
                data->state = AppStateSuccess;
                data->timer_start_tick = furi_get_tick(); 
                notification_message(notifications, &sequence_success);
            }
        }

        if(data->state == AppStateSuccess) {
            if(furi_get_tick() - data->timer_start_tick > AUTO_CLOSE_MS) {
                data->state = AppStateScanning;
                nfc_poller_stop(data->poller);
                nfc_poller_start(data->poller, nfc_callback, data);
            }
        }

        view_port_update(view_port);
    }

    nfc_poller_stop(data->poller);
    nfc_poller_free(data->poller);
    nfc_free(data->nfc);

    furi_record_close(RECORD_NOTIFICATION);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    
    furi_message_queue_free(data->event_queue);
    free(data);

    return 0;
}
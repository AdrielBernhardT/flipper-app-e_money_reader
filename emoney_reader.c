#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
// 1. TAMBAHKAN LIBRARY NOTIFIKASI
#include <notification/notification.h>
#include <notification/notification_messages.h>

// --- KONFIGURASI ---
#define AUTO_CLOSE_MS 5000
#define TAG "IndoReader"

// --- STATE APLIKASI ---
typedef enum {
    AppStateScanning,
    AppStateSuccess,
} AppState;

typedef struct {
    AppState state;
    uint32_t saldo;
    uint32_t timer_start_tick;
} AppData;

typedef enum {
    EventTypeInput,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} AppEvent;

// --- PELUKIS (Draw Callback) ---
static void draw_callback(Canvas* canvas, void* context) {
    AppData* data = (AppData*)context;

    canvas_clear(canvas);
    
    // Header
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, "Indo Money Reader");
    canvas_draw_line(canvas, 0, 12, 128, 12);

    if(data->state == AppStateScanning) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, "Checking Balance...");
        
        canvas_draw_rframe(canvas, 49, 40, 30, 20, 2); 
        canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignCenter, "NFC");
        
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "[Tekan OK = Simulasi]");
    } 
    else if(data->state == AppStateSuccess) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignCenter, "Saldo Anda:");
        
        canvas_set_font(canvas, FontBigNumbers);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "Rp %ld.000", data->saldo / 1000);
        canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignCenter, buffer);

        // Progress Bar 5 Detik
        uint32_t elapsed = furi_get_tick() - data->timer_start_tick;
        if (elapsed > AUTO_CLOSE_MS) elapsed = AUTO_CLOSE_MS;
        float progress = 1.0f - ((float)elapsed / AUTO_CLOSE_MS);
        
        canvas_draw_frame(canvas, 14, 58, 100, 4);
        canvas_draw_box(canvas, 14, 58, (int)(100 * progress), 4);
    }
}

// --- INPUT HANDLER ---
static void input_callback(InputEvent* input_event, void* context) {
    FuriMessageQueue* event_queue = (FuriMessageQueue*)context;
    AppEvent event = {.type = EventTypeInput, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

// --- MAIN PROGRAM ---
int32_t emoney_reader_app(void* p) {
    UNUSED(p);

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(AppEvent));

    AppData* data = malloc(sizeof(AppData));
    data->state = AppStateScanning;
    data->saldo = 0;

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, data);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    // 2. BUKA AKSES KE LAYANAN NOTIFIKASI
    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);

    FURI_LOG_I(TAG, "App Started with Notifications");

    AppEvent event;
    bool processing = true;
    
    while(processing) {
        FuriStatus status = furi_message_queue_get(event_queue, &event, 100);

        if(status == FuriStatusOk) {
            if(event.type == EventTypeInput) {
                if(event.input.key == InputKeyBack && event.input.type == InputTypeShort) {
                    processing = false;
                }
                else if(event.input.key == InputKeyOk && event.input.type == InputTypeShort) {
                    // --- SIMULASI SUKSES ---
                    if(data->state == AppStateScanning) {
                        data->saldo = 850000;
                        data->state = AppStateSuccess;
                        data->timer_start_tick = furi_get_tick();
                        
                        // 3. MAIN KAN NOTIFIKASI "SUKSES"
                        // sequence_success = Lampu Hijau Kedip + Getar Pendek + Suara 'Ting'
                        notification_message(notifications, &sequence_success);
                    }
                }
            }
        }

        // LOGIKA TIMER
        if(data->state == AppStateSuccess) {
            uint32_t current_time = furi_get_tick();
            if(current_time - data->timer_start_tick > AUTO_CLOSE_MS) {
                data->state = AppStateScanning; 
                data->saldo = 0;
            }
        }

        view_port_update(view_port);
    }

    // 4. CLEANUP (JANGAN LUPA TUTUP NOTIFIKASI)
    furi_record_close(RECORD_NOTIFICATION);
    
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(event_queue);
    free(data);

    return 0;
}
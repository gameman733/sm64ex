#include "text_input.h"

#include "controller/controller_keyboard.h"

#ifdef WAPI_SDL2
#include <SDL2/SDL.h>
#endif

#define QUEUE_SIZE 64

static struct TextInputEvent sQueue[QUEUE_SIZE];
static int sHead;
static int sCount;
static bool sActive;

void text_input_set_active(bool active) {
    if (active == sActive) return;
    sActive = active;
    sHead = 0;
    sCount = 0;

    if (active) {
        // don't carry keys that are already held (e.g. the one that opened the menu) into the text field
        keyboard_on_all_keys_up();
    }
#ifdef WAPI_SDL2
    if (active) SDL_StartTextInput();
    else SDL_StopTextInput();
#endif
}

bool text_input_is_active(void) {
    return sActive;
}

void text_input_push(enum TextInputEventType type, char ch) {
    if (!sActive || sCount == QUEUE_SIZE) return;
    if (type == TEXT_INPUT_CHAR && (ch < 0x20 || ch > 0x7E)) return; // the menu font is ASCII only

    sQueue[(sHead + sCount) % QUEUE_SIZE] = (struct TextInputEvent) { type, ch };
    sCount++;
}

bool text_input_pop(struct TextInputEvent *ev) {
    if (sCount == 0) return false;

    *ev = sQueue[sHead];
    sHead = (sHead + 1) % QUEUE_SIZE;
    sCount--;
    return true;
}

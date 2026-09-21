#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/external.h"
#include "connect_menu.h"
#include "game/game_init.h"
#include "game/ingame_menu.h"
#include "game/save_file.h"
#include "game/segment2.h"
#include "pc/text_input.h"
#include "sm64.h"

#if !defined(VERSION_JP) && !defined(VERSION_SH)
s16 get_string_width(u8 *str);
#endif
void create_dl_scale_matrix(s8 pushOp, f32 x, f32 y, f32 z);

/**
 * @file connect_menu.c
 * Everything here is drawn as flat 2D rectangles and text on top of the file select screen, and is
 * operated either with the keyboard (typing, Tab, Enter, Esc) or with the file select cursor.
 * All rectangles are in screen space (320x240, y pointing down), except for text baselines
 * which, like print_generic_string, are measured from the bottom of the screen.
 */

#define CLICK_NONE (-10000)

// The menu font is drawn at this fraction of its size so more fits in the fields and tiles.
#define TEXT_SCALE 0.8f
// Height of a glyph cell, which sits on top of the baseline.
#define TEXT_HEIGHT ((s16) (16 * TEXT_SCALE))

#define DEFAULT_SERVER "archipelago.gg"

enum Field {
    FIELD_SERVER,
    FIELD_PORT,
    FIELD_NAME,
    FIELD_PASSWORD,
    NUM_FIELDS,
};

// Focusable elements of the form: the fields followed by two buttons.
enum FormFocus {
    FOCUS_SAVE = NUM_FIELDS,
    FOCUS_BACK,
    NUM_FORM_FOCUS,
};

// Focusable elements of the slot picker: the four files followed by a button.
enum SlotFocus {
    SLOT_FOCUS_BACK = NUM_SAVE_FILES,
    NUM_SLOT_FOCUS,
};

enum Phase {
    PHASE_FORM,
    PHASE_SLOT,
};

struct Rect {
    s16 x1, y1, x2, y2;
};

#define PORT_MAX_LEN 5
static const u8 sFieldMaxLen[NUM_FIELDS] = {
    AP_SERVER_LEN - 1 - (PORT_MAX_LEN + 1), // leaves room for ":port"
    PORT_MAX_LEN,
    AP_NAME_LEN - 1,
    AP_PASSWORD_LEN - 1,
};

static const char *sFieldLabels[NUM_FIELDS] = { "SERVER", "PORT", "NAME", "PASSWORD" };

// Baselines of the fields and their labels, measured from the bottom of the screen.
static const s16 sFieldBaseline[NUM_FIELDS] = { 172, 144, 116, 88 };

#define FIELD_X1 108
#define FIELD_X2 292
#define FIELD_LABEL_X 16
#define FIELD_TEXT_PAD 6

// The button on the file select screen that opens the form.
static const struct Rect sOpenButton = { 230, 26, 300, 48 };

static const struct Rect sSaveButton = { 40, 180, 140, 204 };
static const struct Rect sBackButton = { 180, 180, 280, 204 };
static const struct Rect sSlotBackButton = { 110, 180, 210, 204 };
static const struct Rect sSlotTiles[NUM_SAVE_FILES] = {
    { 36, 68, 152, 112 }, { 168, 68, 284, 112 },
    { 36, 124, 152, 168 }, { 168, 124, 284, 168 },
};

#define NOTICE_FRAMES 90

static const struct Rect sNoticeBox = { 48, 92, 272, 148 };

static s32 sOpen = FALSE;
static s32 sNoticeTimer = 0;
static enum Phase sPhase = PHASE_FORM;
static s32 sFocus = FIELD_SERVER;
static char sFields[NUM_FIELDS][AP_SERVER_LEN] = { DEFAULT_SERVER, "", "", "" };
static char sMessage[40] = "";
static s32 sMessageIsError = FALSE;

/* helpers */

static struct Rect field_rect(s32 field) {
    const s16 baseline = sFieldBaseline[field];
    struct Rect r = { FIELD_X1, 222 - baseline, FIELD_X2, 244 - baseline };
    return r;
}

static s32 rect_contains(const struct Rect *r, s16 x, s16 y) {
    return x >= r->x1 && x < r->x2 && y >= r->y1 && y < r->y2;
}

// Cursor space (origin in the middle, y up) to screen space (origin top left, y down).
static s16 cursor_to_screen_x(f32 x) {
    return (s16) (x + 160.0f);
}

static s16 cursor_to_screen_y(f32 y) {
    return (s16) (120.0f - y);
}

static void set_message(const char *msg, s32 isError) {
    snprintf(sMessage, sizeof(sMessage), "%s", msg);
    sMessageIsError = isError;
}

// Width in screen pixels, with TEXT_SCALE applied.
static s16 text_width(const u8 *str) {
#if !defined(VERSION_JP) && !defined(VERSION_SH)
    return (s16) (get_string_width((u8 *) str) * TEXT_SCALE);
#else
    s16 width = 0;
    while (*str++ != DIALOG_CHAR_TERMINATOR) width += 10;
    return (s16) (width * TEXT_SCALE);
#endif
}

static s32 dialog_length(const u8 *str) {
    s32 len = 0;
    while (str[len] != DIALOG_CHAR_TERMINATOR) len++;
    return len;
}

// The menu font only has letters, digits and a bit of punctuation. Everything else shows up as '?'
// (the stored text is still exactly what was typed).
static u8 ascii_to_dialog(char c) {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        return ASCII_TO_DIALOG(c);
    }
    switch (c) {
        case ' ':  return DIALOG_CHAR_SPACE;
        case '\'': return 0x3E;
        case '.':  return 0x3F;
        case ',':  return DIALOG_CHAR_COMMA;
        case '-':  return 0x9F;
        case '(':  return 0xE1;
        case ')':  return 0xE3;
        case '+':  return 0xE4;
        case '&':  return 0xE5;
        case ':':  return 0xE6;
        case '!':  return 0xF2;
        case '%':  return 0xF3;
        case '?':  return 0xF4;
        case '~':  return 0xF7;
        case '$':  return 0xF9;
        default:   return 0xF4;
    }
}

static void to_dialog(const char *src, u8 *dst, size_t dstSize) {
    size_t i;
    for (i = 0; src[i] != '\0' && i < dstSize - 1; i++) {
        dst[i] = ascii_to_dialog(src[i]);
    }
    dst[i] = DIALOG_CHAR_TERMINATOR;
}

/* drawing */

static void draw_rect(s16 x1, s16 y1, s16 x2, s16 y2, u8 r, u8 g, u8 b) {
    gDPPipeSync(gDisplayListHead++);
    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetCycleType(gDisplayListHead++, G_CYC_FILL);
    gDPSetFillColor(gDisplayListHead++, GPACK_RGBA5551(r, g, b, 255));
    gDPFillRectangle(gDisplayListHead++, x1, y1, x2 - 1, y2 - 1);
    gDPPipeSync(gDisplayListHead++);
    gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
}

// A rectangle with a 2 pixel border.
static void draw_box(const struct Rect *r, u8 bgR, u8 bgG, u8 bgB, u8 borderR, u8 borderG, u8 borderB) {
    draw_rect(r->x1 - 2, r->y1 - 2, r->x2 + 2, r->y2 + 2, borderR, borderG, borderB);
    draw_rect(r->x1, r->y1, r->x2, r->y2, bgR, bgG, bgB);
}

static void print_scaled(s16 x, s16 y, const u8 *str) {
    create_dl_translation_matrix(MENU_MTX_PUSH, x, y, 0.0f);
    create_dl_scale_matrix(MENU_MTX_NOPUSH, TEXT_SCALE, TEXT_SCALE, 1.0f);
    print_generic_string(0, 0, str);
    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
}

static void draw_text(s16 x, s16 y, const u8 *str, u8 r, u8 g, u8 b) {
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 0, 0, 0, 255);
    print_scaled(x + 1, y - 1, str);
    gDPSetEnvColor(gDisplayListHead++, r, g, b, 255);
    print_scaled(x, y, str);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

static void draw_text_centered(s16 centerX, s16 y, const char *str, u8 r, u8 g, u8 b) {
    u8 buf[AP_SERVER_LEN + 1];
    to_dialog(str, buf, sizeof(buf));
    draw_text(centerX - text_width(buf) / 2, y, buf, r, g, b);
}

// Draws the end of a string if it doesn't fit in maxWidth, returns the width that was drawn.
static s16 draw_text_tail(s16 x, s16 y, const char *str, s16 maxWidth, u8 r, u8 g, u8 b, s32 mask) {
    u8 buf[AP_SERVER_LEN + 1];
    s32 start = 0;

    to_dialog(str, buf, sizeof(buf));
    if (mask) {
        for (s32 i = 0; buf[i] != DIALOG_CHAR_TERMINATOR; i++) buf[i] = 0x3F; // '.'
    }
    while (buf[start] != DIALOG_CHAR_TERMINATOR && text_width(buf + start) > maxWidth) start++;

    draw_text(x, y, buf + start, r, g, b);
    return text_width(buf + start);
}

// Draws the start of a string, cut off if it doesn't fit in maxWidth.
static void draw_text_head(s16 x, s16 y, const char *str, s16 maxWidth, u8 r, u8 g, u8 b) {
    u8 buf[AP_SERVER_LEN + 1];
    s32 len;

    to_dialog(str, buf, sizeof(buf));
    len = dialog_length(buf);
    while (len > 0 && text_width(buf) > maxWidth) {
        buf[--len] = DIALOG_CHAR_TERMINATOR;
    }
    draw_text(x, y, buf, r, g, b);
}

static void draw_button(const struct Rect *r, const char *label, s32 focused, s32 hovered) {
    const u8 shade = hovered ? 0x30 : 0;
    // the label sits on the vertical middle of the button (baselines are measured from the bottom)
    const s16 baseline = 240 - (r->y1 + r->y2) / 2 - TEXT_HEIGHT / 2;

    draw_box(r, 0x90 + shade, 0x60 + shade, 0xD0, focused ? 255 : 0x50, focused ? 255 : 0x30, focused ? 255 : 0x90);
    draw_text_centered((r->x1 + r->x2) / 2, baseline, label, 255, 255, 255);
}

static void draw_title(const char *title) {
    u8 buf[32];
    to_dialog(title, buf, sizeof(buf));

    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
    print_hud_lut_string(HUD_LUT_GLOBAL, 160 - (s16) strlen(title) * 6, 25, buf); // the HUD font is 12 wide
    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_end);
}

static void draw_message(void) {
    if (sMessage[0] == '\0') return;

    if (sMessageIsError) {
        draw_text_centered(160, 64, sMessage, 255, 120, 120);
    } else {
        draw_text_centered(160, 64, sMessage, 160, 255, 160);
    }
}

static void draw_form(s16 cursorX, s16 cursorY) {
    for (s32 i = 0; i < NUM_FIELDS; i++) {
        const struct Rect r = field_rect(i);
        const s32 focused = (sFocus == i);
        const s16 baseline = sFieldBaseline[i];
        u8 label[16];

        to_dialog(sFieldLabels[i], label, sizeof(label));
        draw_text(FIELD_LABEL_X, baseline, label, 255, 255, 255);

        draw_box(&r, focused ? 0x50 : 0x30, focused ? 0x2A : 0x18, focused ? 0x88 : 0x58,
                 focused ? 255 : 0x50, focused ? 255 : 0x30, focused ? 255 : 0x90);

        const s16 textWidth = draw_text_tail(r.x1 + FIELD_TEXT_PAD, baseline, sFields[i],
                                             r.x2 - r.x1 - FIELD_TEXT_PAD * 2 - 4, 255, 255, 255, i == FIELD_PASSWORD);
        if (focused && (gGlobalTimer & 16)) {
            // blinking caret, the font has no underscore
            const s16 caretX = r.x1 + FIELD_TEXT_PAD + textWidth + 1;
            draw_rect(caretX, 240 - baseline - TEXT_HEIGHT, caretX + 2, 240 - baseline, 255, 255, 255);
        }
    }

    draw_button(&sSaveButton, "SAVE", sFocus == FOCUS_SAVE, rect_contains(&sSaveButton, cursorX, cursorY));
    draw_button(&sBackButton, "BACK", sFocus == FOCUS_BACK, rect_contains(&sBackButton, cursorX, cursorY));
}

static void draw_slot_picker(s16 cursorX, s16 cursorY) {
    draw_text_centered(160, 180, "SAVE TO WHICH FILE?", 255, 255, 255);

    for (s32 i = 0; i < NUM_SAVE_FILES; i++) {
        const struct Rect *r = &sSlotTiles[i];
        const s32 focused = (sFocus == i);
        const s32 hovered = rect_contains(r, cursorX, cursorY);
        const u8 shade = hovered ? 0x30 : 0;
        const s16 baseline = 240 - (r->y1 + 8) - TEXT_HEIGHT;
        const char *server = save_file_get_ap_server(i);
        char name[8];

        draw_box(r, 0x90 + shade, 0x60 + shade, 0xD0, focused ? 255 : 0x50, focused ? 255 : 0x30, focused ? 255 : 0x90);

        snprintf(name, sizeof(name), "FILE %c", 'A' + i);
        draw_text_centered((r->x1 + r->x2) / 2, baseline, name, 255, 255, 255);
        if (server[0] != '\0') {
            u8 buf[AP_SERVER_LEN + 1];
            s32 len;

            to_dialog(server, buf, sizeof(buf));
            len = dialog_length(buf);
            while (len > 0 && text_width(buf) > r->x2 - r->x1 - 12) buf[--len] = DIALOG_CHAR_TERMINATOR;
            draw_text(r->x1 + 6, baseline - 18, buf, 220, 255, 200);
        } else {
            draw_text_centered((r->x1 + r->x2) / 2, baseline - 18, "NONE", 200, 180, 240);
        }
    }

    draw_button(&sSlotBackButton, "BACK", sFocus == SLOT_FOCUS_BACK, rect_contains(&sSlotBackButton, cursorX, cursorY));
}

/* input */

static void open_form(void) {
    sOpen = TRUE;
    sPhase = PHASE_FORM;
    sFocus = FIELD_SERVER;
    sMessage[0] = '\0';
    text_input_set_active(TRUE);
    play_sound(SOUND_MENU_CLICK_FILE_SELECT, gDefaultSoundArgs);
}

static void close_form(void) {
    sOpen = FALSE;
    text_input_set_active(FALSE);
    play_sound(SOUND_MENU_CLICK_FILE_SELECT, gDefaultSoundArgs);
}

static void append_char(s32 field, char c) {
    const size_t len = strlen(sFields[field]);

    if (len >= sFieldMaxLen[field]) return;
    if (field == FIELD_PORT && (c < '0' || c > '9')) return;

    sFields[field][len] = c;
    sFields[field][len + 1] = '\0';
    sMessage[0] = '\0';
}

static void delete_char(s32 field) {
    const size_t len = strlen(sFields[field]);

    if (len > 0) sFields[field][len - 1] = '\0';
    sMessage[0] = '\0';
}

static void show_error(const char *msg, s32 focus) {
    set_message(msg, TRUE);
    sFocus = focus;
    play_sound(SOUND_MENU_CAMERA_BUZZ, gDefaultSoundArgs);
}

// Checks the form and moves on to picking the file to save it to.
static void try_save(void) {
    const s32 port = atoi(sFields[FIELD_PORT]);

    if (sFields[FIELD_SERVER][0] == '\0') {
        show_error("ENTER A SERVER", FIELD_SERVER);
    } else if (sFields[FIELD_PORT][0] != '\0' && (port < 1 || port > 65535)) {
        show_error("PORT MUST BE 1 TO 65535", FIELD_PORT);
    } else if (sFields[FIELD_NAME][0] == '\0') {
        show_error("ENTER A NAME", FIELD_NAME);
    } else {
        sPhase = PHASE_SLOT;
        sFocus = 0;
        sMessage[0] = '\0';
        play_sound(SOUND_MENU_CLICK_FILE_SELECT, gDefaultSoundArgs);
    }
}

static void save_to_file(s32 fileIndex) {
    char server[AP_SERVER_LEN];

    if (sFields[FIELD_PORT][0] != '\0') {
        snprintf(server, sizeof(server), "%s:%s", sFields[FIELD_SERVER], sFields[FIELD_PORT]);
    } else {
        snprintf(server, sizeof(server), "%s", sFields[FIELD_SERVER]);
    }
    save_file_set_ap_connection(fileIndex, server, sFields[FIELD_NAME], sFields[FIELD_PASSWORD]);

    snprintf(sMessage, sizeof(sMessage), "SAVED TO FILE %c", 'A' + fileIndex);
    sMessageIsError = FALSE;
    sPhase = PHASE_FORM;
    sFocus = FOCUS_SAVE;
    play_sound(SOUND_MENU_STAR_SOUND, gDefaultSoundArgs);
}

static void back_to_form(void) {
    sPhase = PHASE_FORM;
    sFocus = FOCUS_SAVE;
    sMessage[0] = '\0';
    play_sound(SOUND_MENU_CLICK_FILE_SELECT, gDefaultSoundArgs);
}

static void activate_slot_focus(s32 focus) {
    if (focus == SLOT_FOCUS_BACK) {
        back_to_form();
    } else {
        save_to_file(focus);
    }
}

static void move_focus(s32 delta) {
    const s32 count = (sPhase == PHASE_FORM) ? NUM_FORM_FOCUS : NUM_SLOT_FOCUS;

    sFocus = (sFocus + delta + count) % count;
    play_sound(SOUND_MENU_CHANGE_SELECT, gDefaultSoundArgs);
}

static void handle_key(const struct TextInputEvent *ev) {
    switch (ev->type) {
        case TEXT_INPUT_CHAR:
            if (sPhase == PHASE_FORM) {
                if (sFocus < NUM_FIELDS) append_char(sFocus, ev->ch);
            } else if (ev->ch >= 'a' && ev->ch <= 'd') {
                save_to_file(ev->ch - 'a');
            } else if (ev->ch >= 'A' && ev->ch <= 'D') {
                save_to_file(ev->ch - 'A');
            } else if (ev->ch >= '1' && ev->ch <= '4') {
                save_to_file(ev->ch - '1');
            }
            break;

        case TEXT_INPUT_BACKSPACE:
            if (sPhase == PHASE_FORM && sFocus < NUM_FIELDS) delete_char(sFocus);
            break;

        case TEXT_INPUT_TAB:
        case TEXT_INPUT_DOWN:
        case TEXT_INPUT_RIGHT:
            move_focus(1);
            break;

        case TEXT_INPUT_SHIFT_TAB:
        case TEXT_INPUT_UP:
        case TEXT_INPUT_LEFT:
            move_focus(-1);
            break;

        case TEXT_INPUT_ENTER:
            if (sPhase == PHASE_SLOT) {
                activate_slot_focus(sFocus);
            } else if (sFocus == FOCUS_BACK) {
                close_form();
            } else if (sFocus == FIELD_PASSWORD || sFocus == FOCUS_SAVE) {
                try_save();
            } else {
                move_focus(1);
            }
            break;

        case TEXT_INPUT_ESCAPE:
            if (sPhase == PHASE_SLOT) {
                back_to_form();
            } else {
                close_form();
            }
            break;
    }
}

static void handle_click(s16 x, s16 y) {
    if (sPhase == PHASE_FORM) {
        for (s32 i = 0; i < NUM_FIELDS; i++) {
            const struct Rect r = field_rect(i);
            if (rect_contains(&r, x, y)) {
                sFocus = i;
                play_sound(SOUND_MENU_CHANGE_SELECT, gDefaultSoundArgs);
                return;
            }
        }
        if (rect_contains(&sSaveButton, x, y)) {
            try_save();
        } else if (rect_contains(&sBackButton, x, y)) {
            close_form();
        }
    } else {
        for (s32 i = 0; i < NUM_SAVE_FILES; i++) {
            if (rect_contains(&sSlotTiles[i], x, y)) {
                save_to_file(i);
                return;
            }
        }
        if (rect_contains(&sSlotBackButton, x, y)) {
            back_to_form();
        }
    }
}

/* interface */

s32 connect_menu_is_open(void) {
    return sOpen;
}

void connect_menu_close(void) {
    sOpen = FALSE;
    sNoticeTimer = 0;
    text_input_set_active(FALSE);
}

void connect_menu_show_no_connection_notice(void) {
    sNoticeTimer = NOTICE_FRAMES;
}

void connect_menu_update(s16 clickX, s16 clickY) {
    const s32 clicked = (clickX != CLICK_NONE);
    const s16 x = cursor_to_screen_x(clickX);
    const s16 y = cursor_to_screen_y(clickY);

    if (!sOpen) {
        if (sNoticeTimer > 0) sNoticeTimer--;
        if (clicked && rect_contains(&sOpenButton, x, y)) open_form();
        return;
    }

    struct TextInputEvent ev;
    while (sOpen && text_input_pop(&ev)) {
        handle_key(&ev);
    }
    if (sOpen && clicked) handle_click(x, y);
}

void connect_menu_draw(f32 cursorX, f32 cursorY) {
    const s16 x = cursor_to_screen_x(cursorX);
    const s16 y = cursor_to_screen_y(cursorY);

    if (!sOpen) {
        draw_button(&sOpenButton, "CONNECT", FALSE, rect_contains(&sOpenButton, x, y));
        if (sNoticeTimer > 0) {
            draw_box(&sNoticeBox, 0x60, 0x30, 0xA0, 255, 255, 255);
            draw_text_centered(160, 116, "NO CONNECTION SAVED", 255, 120, 120);
            draw_text_centered(160, 98, "USE CONNECT FIRST", 255, 255, 255);
        }
        return;
    }

    draw_rect(0, 0, 320, 240, 0x60, 0x30, 0xA0);
    draw_title("CONNECT");
    if (sPhase == PHASE_FORM) {
        draw_form(x, y);
    } else {
        draw_slot_picker(x, y);
    }
    draw_message();
}

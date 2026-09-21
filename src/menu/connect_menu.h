#ifndef CONNECT_MENU_H
#define CONNECT_MENU_H

#include <PR/ultratypes.h>

#include "types.h"

// Archipelago connections on the file select screen: a "CONNECTIONS" button that opens a picker for one
// of the save files, then a full screen form (server, port, name, password) to create or edit the
// connection saved in that file.

s32 connect_menu_is_open(void);
void connect_menu_close(void);

// Briefly tells the player that the file they picked has no connection saved to it.
void connect_menu_show_no_connection_notice(void);

// Call once per frame with the last cursor click position (in cursor space, -10000 if there was none).
void connect_menu_update(s16 clickX, s16 clickY);

// Draws the CONNECT button, or the form if it is open. The cursor position is in cursor space.
void connect_menu_draw(f32 cursorX, f32 cursorY);

#endif // CONNECT_MENU_H

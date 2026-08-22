#ifndef SC_WINDOW_H
#define SC_WINDOW_H

#include "common.h"

#include <stdbool.h>
#include <SDL3/SDL_video.h>

// Enable rounded corners on Windows 11+ (no-op on older systems)
void
sc_win_set_window_rounded_corners(SDL_Window *window, bool enable);

// Make the borderless window draggable by its client area
void
sc_win_enable_drag_move(SDL_Window *window);

#endif
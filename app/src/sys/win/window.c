#include "window.h"

#include <windows.h>
#include <dwmapi.h>

#include "util/log.h"

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
# define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWCP_ROUND
# define DWMWCP_DONOTROUND 1
# define DWMWCP_ROUND 2
#endif

static HWND
get_hwnd(SDL_Window *window) {
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (!props) {
        return NULL;
    }
    return (HWND) SDL_GetPointerProperty(props,
                                         SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                         NULL);
}

// Whether rounded corners are currently active for this process's window
// (scrcpy uses at most one window at a time).
static bool sc_win_rounded = false;

// Windows 10 fallback: clip the window to a rounded-rect region so the
// corners are visually rounded even without DWM corner support (Win11+).
static void
apply_rounded_region(HWND hwnd, bool enable) {
    RECT rect;
    if (!GetWindowRect(hwnd, &rect)) {
        return;
    }

    if (!enable) {
        // Remove the region entirely
        SetWindowRgn(hwnd, NULL, TRUE);
        return;
    }

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    // Corner radius ~8px, scaled down for small windows
    int radius = width < 64 || height < 64 ? 4 : 8;

    // Round to whole pixels on the right/bottom edges
    HRGN rgn = CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                  2 * radius, 2 * radius);
    if (!rgn) {
        return;
    }
    SetWindowRgn(hwnd, rgn, TRUE);
    // The system owns the region after a successful SetWindowRgn
}

void
sc_win_set_window_rounded_corners(SDL_Window *window, bool enable) {
    HWND hwnd = get_hwnd(window);
    if (!hwnd) {
        LOGW("Could not retrieve Win32 HWND for rounded corners");
        return;
    }

    if (!enable) {
        apply_rounded_region(hwnd, false);
        return;
    }

    // Preferred path: DWMWCP_ROUND gives the standard Windows 11 rounded
    // corners (anti-aliased, snap-aware).
    int preference = DWMWCP_ROUND;
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                       &preference, sizeof(preference));
    if (SUCCEEDED(hr)) {
        return;
    }

    // Windows 10 fallback: hard-clipped rounded region
    LOGD("DWM corner preference unavailable (hr=0x%08lx), using window "
         "region fallback", (unsigned long) hr);
    apply_rounded_region(hwnd, true);
    sc_win_rounded = true;
}

// Window input routing:
//  - left button passes through untouched (taps/sweeps/pinch go to the device)
//  - right click (release without significant movement) -> synthesized as a
//    normal right click for SDL (default binding: BACK)
//  - right hold + move -> borderless window drag (modal move loop)
typedef struct {
    bool right_down;   // right button currently held (we captured it)
    bool dragging;     // a window drag was initiated
    bool synthesizing; // replaying a synthetic right click: pass through
    POINT origin;      // client coords where right button was pressed
    POINT cursor_start;// screen coords where right button was pressed
    RECT window_base;  // window rect when the drag started
} sc_win_drag_state_t;

static sc_win_drag_state_t sc_win_drag_state;

#define SC_WIN_DRAG_THRESHOLD 4

static void
reset_drag_state(void) {
    sc_win_drag_state.right_down = false;
    sc_win_drag_state.dragging = false;
}

static LRESULT CALLBACK
sc_win_subclass_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                     UINT_PTR id, DWORD_PTR data) {
    (void) id;
    (void) data;

    switch (msg) {
        case WM_RBUTTONDOWN: {
            if (sc_win_drag_state.synthesizing) {
                // Our own replayed click: forward it to SDL untouched
                break;
            }
            // Swallow: do not deliver yet. Decide between BACK click and
            // window drag based on whether the pointer moves.
            POINT pt = {(int16_t) LOWORD(lParam), (int16_t) HIWORD(lParam)};
            sc_win_drag_state.origin = pt;
            GetCursorPos(&sc_win_drag_state.cursor_start);
            GetWindowRect(hwnd, &sc_win_drag_state.window_base);
            sc_win_drag_state.right_down = true;
            sc_win_drag_state.dragging = false;
            SetCapture(hwnd);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (sc_win_drag_state.right_down) {
                POINT cur;
                if (!GetCursorPos(&cur)) {
                    break;
                }
                int dx = cur.x - sc_win_drag_state.cursor_start.x;
                int dy = cur.y - sc_win_drag_state.cursor_start.y;
                if (!sc_win_drag_state.dragging) {
                    int adx = dx < 0 ? -dx : dx;
                    int ady = dy < 0 ? -dy : dy;
                    if (adx > SC_WIN_DRAG_THRESHOLD
                            || ady > SC_WIN_DRAG_THRESHOLD) {
                        sc_win_drag_state.dragging = true;
                    }
                }
                if (sc_win_drag_state.dragging) {
                    // Direct position tracking: no modal loop, no surprises.
                    SetWindowPos(hwnd, NULL,
                                 sc_win_drag_state.window_base.left + dx,
                                 sc_win_drag_state.window_base.top + dy,
                                 0, 0,
                                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    return 0;
                }
            }
            break;
        }
        case WM_RBUTTONUP: {
            if (sc_win_drag_state.synthesizing) {
                // End of our replayed click pair: forward, stop masking
                sc_win_drag_state.synthesizing = false;
                break;
            }
            if (!sc_win_drag_state.right_down) {
                break; // not ours (e.g. synthesized pair), let it through
            }
            ReleaseCapture();
            if (!sc_win_drag_state.dragging) {
                // Plain right click: hand a down/up pair to SDL so scrcpy
                // performs its default right-click action (BACK).
                sc_win_drag_state.synthesizing = true;
                PostMessage(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, lParam);
                PostMessage(hwnd, WM_RBUTTONUP, 0, lParam);
            }
            reset_drag_state();
            return 0;
        }
        case WM_CAPTURECHANGED:
            // Capture lost mid-gesture: drop pending right-button tracking
            // so stale state cannot swallow later clicks.
            reset_drag_state();
            break;
        case WM_SIZE:
            if (sc_win_rounded && wParam != SIZE_MINIMIZED) {
                // Regions do not scale with the window; re-apply
                apply_rounded_region(hwnd, true);
            }
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void
sc_win_enable_drag_move(SDL_Window *window) {
    HWND hwnd = get_hwnd(window);
    if (!hwnd) {
        LOGW("Could not retrieve Win32 HWND for drag-move");
        return;
    }

    BOOL ok = SetWindowSubclass(hwnd, sc_win_subclass_proc, 1, 0);
    if (!ok) {
        LOGW("Could not subclass window for drag-move");
    } else {
        LOGD("Subclassed HWND %p for drag-move (borderless)", (void *) hwnd);
    }
}
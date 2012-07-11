#ifndef __WM_STRUCTURE_H__
#define __WM_STRUCTURE_H__

#include <stdint.h>
#include <stdbool.h>

#include <xcb/xcb.h>

#include "list.h"

#ifndef OFFSET_OF
#define OFFSET_OF(type, member)                                      \
    ((size_t)(&((type *)0)->member))
#endif

#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, member)                 \
    ((type *)((char *)(ptr) - OFFSET_OF(type, member)))
#endif


typedef struct rect_s
{
    int x, y;
    unsigned int w, h;
} rect_s;

typedef rect_s *rect_t;

#define GEOM_STATE_NORMAL 0
#define GEOM_STATE_VMAX   1
#define GEOM_STATE_HMAX   2
#define GEOM_STATE_MAX    3
#define GEOM_STATE_MIN    4

#define SCREEN_MODE_NORMAL                 0
#define SCREEN_MODE_MOVE_WINDOW_BY_MOUSE   1
#define SCREEN_MODE_RESIZE_WINDOW_BY_MOUSE 2
#define SCREEN_MODE_COUNT                  3

typedef struct screen_s
{
    uint32_t active_border_pixel;
    uint32_t inactive_border_pixel;

    list_entry_s clients;

    int mode_pointer_x;
    int mode_pointer_y;
    int mode;
    
    struct client_s *focus;
    xcb_screen_t *xcb_screen;
} screen_s;

typedef screen_s *screen_t;

typedef struct client_s
{
    screen_t screen;
    xcb_drawable_t xcb_container; /* container that hold the original window */
    xcb_drawable_t xcb_orig;      /* xcb handler */
    
    int geom_state;
    int mapped;
    
    list_entry_s client_node;
} client_s;

typedef client_s *client_t;

client_t client_attach(xcb_window_t window);
void     client_detach(client_t client);
void     client_focus(client_t client);
void     client_map(client_t client);
void     client_unmap(client_t client);

#endif

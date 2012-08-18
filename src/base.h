#ifndef __WM_BASE_H__
#define __WM_BASE_H__

#include <stdint.h>
#include <stdbool.h>

#include <xcb/xcb.h>

#include "list.h"

#define DYN_STRING(string_const)                                        \
    ({ char *r = (char *)malloc(sizeof(string_const));                  \
        if (r) memcpy(r, string_const, sizeof(string_const)); r; })

#ifndef OFFSET_OF
#define OFFSET_OF(type, member)                                      \
    ((size_t)(&((type *)0)->member))
#endif

#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, member)                 \
    ((type *)((char *)(ptr) - OFFSET_OF(type, member)))
#endif

#define DEBUGP(args ...) fprintf(stderr, args)

typedef struct rect_s
{
    int x, y;
    unsigned int w, h;
} rect_s;

typedef rect_s *rect_t;

typedef void(*mouse_motion_callback_f)(void *data, int abs_x, int abs_y);
typedef void(*mouse_release_callback_f)(void *data);

typedef struct screen_s
{
    xcb_screen_t     *xcb_screen;
    
    int                      mouse_attached;
    mouse_motion_callback_f  mouse_motion_callback;
    mouse_release_callback_f mouse_release_callback;
    void                    *mouse_cb_data;

    struct client_s *focus;
    list_entry_s client_list;
    list_entry_s auto_scan_list;
} screen_s;

typedef screen_s *screen_t;

typedef struct client_s
{
    screen_t               screen;
    xcb_drawable_t         xcb_window;
    list_entry_s           client_node;
    struct client_class_s *class;
    void                  *priv;
} client_s;

typedef client_s *client_t;

typedef struct wnd_dict_node_s *wnd_dict_node_t;
typedef struct wnd_dict_node_s
{
    xcb_window_t wnd;
    int          role;
    void        *link;
    struct wnd_dict_node_s *next;
} wnd_dict_node_s;

wnd_dict_node_t wnd_dict_find(xcb_window_t wnd, int op);

#define WND_ROLE_INIT          0
#define WND_ROLE_ROOT          1
#define WND_ROLE_CLIENT        2
#define WND_ROLE_CLIENT_IGNORE 3
#define WND_DICT_FIND_OP_NONE  0
#define WND_DICT_FIND_OP_TOUCH 1
#define WND_DICT_FIND_OP_ERASE 2


typedef struct client_class_s *client_class_t;
typedef struct client_class_s
{
    const char *(*class_name_get)(client_class_t self);
    
    list_entry_s auto_scan_node;

    void(*init)(client_class_t self);
    int (*client_try_attach)(client_class_t self, client_t client);
#define CLIENT_TRY_ATTACH_ATTACHED 0
#define CLIENT_TRY_ATTACH_FAILED   1
    void(*client_map)(client_class_t self, client_t client);
    void(*client_unmap)(client_class_t self, client_t client);
    void(*client_detach)(client_class_t self, client_t client, int keep_mapped);
    int (*client_event_button_press)(client_class_t self, client_t client, xcb_button_press_event_t *e);
#define CLIENT_INPUT_CATCHED      0
#define CLIENT_INPUT_PASS_THROUGH 1
    void(*client_event_map_notify)(client_class_t self, client_t client, xcb_map_notify_event_t *e);
    void(*client_event_unmap_notify)(client_class_t self, client_t client, xcb_unmap_notify_event_t *e);
    void(*client_event_reparent_notify)(client_class_t self, client_t client, xcb_reparent_notify_event_t *e);
    void(*client_aevent_focus)(client_class_t self, client_t client);
    void(*client_aevent_blur)(client_class_t self, client_t client);
} client_class_s;

void client_class_auto_scan_attach(screen_t screen, client_class_t cc);
void client_class_auto_scan_detach(screen_t screen, client_class_t cc);
int  screen_mouse_attach(screen_t screen, mouse_motion_callback_f motion_callback, mouse_release_callback_f release_callback, void *data);
#define SCREEN_MOUSE_POINTER_ATTACH_ATTACHED 0
#define SCREEN_MOUSE_POINTER_ATTACH_FAILED   1
void screen_mouse_detach(screen_t screen);
void focus_set(client_t client);

int  xh_window_geom_get(xcb_window_t window, xcb_window_t *parent, rect_t geom);

extern xcb_connection_t *x_conn;
extern screen_t screens;
extern int      screen_count;

#endif

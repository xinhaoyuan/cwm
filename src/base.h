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
typedef void(*mouse_release_callback_f)(void *data, int abs_x, int abs_y);

typedef struct screen_s
{
    xcb_screen_t     *xcb_screen;
    
    int                      mouse_attached;
    mouse_motion_callback_f  mouse_motion_callback;
    mouse_release_callback_f mouse_release_callback;
    void                    *mouse_cb_data;
    
    list_entry_s      client_list;
    list_entry_s      auto_scan_list;
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

typedef struct client_class_s *client_class_t;
typedef struct client_class_s
{
    const char *(*class_name_get)(client_class_t self);
    
    list_entry_s auto_scan_node;
    
    int (*client_try_attach)(client_class_t self, client_t client);
#define CLIENT_TRY_ATTACH_ATTACHED 0
#define CLIENT_TRY_ATTACH_FAILED   1
    void(*client_map)(client_class_t self, client_t client);
    void(*client_unmap)(client_class_t self, client_t client);
    void(*client_detach)(client_class_t self, client_t client);
    int (*client_event_button_press)(client_class_t self, client_t client, xcb_button_press_event_t *e);
#define CLIENT_INPUT_CATCHED      0
#define CLIENT_INPUT_PASS_THROUGH 1
    void(*client_event_map_notify)(client_class_t self, client_t client, xcb_map_notify_event_t *e);
    void(*client_event_unmap_notify)(client_class_t self, client_t client, xcb_unmap_notify_event_t *e);
} client_class_s;

void client_class_auto_scan_attach(screen_t screen, client_class_t cc);
void client_class_auto_scan_detach(screen_t screen, client_class_t cc);
int  screen_mouse_attach(screen_t screen, mouse_motion_callback_f motion_callback, mouse_release_callback_f release_callback, void *data);
#define SCREEN_MOUSE_POINTER_ATTACH_ATTACHED 0
#define SCREEN_MOUSE_POINTER_ATTACH_FAILED   1
void screen_mouse_detach(screen_t screen);

extern screen_t screens;
extern int      screen_count;


#endif

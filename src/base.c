#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>

#include "base.h"
#include "cc/simple.h"

#define HASH_MOD 19997
#define HASH_MUL 10007

static wnd_dict_node_t wnd_hash_list[HASH_MOD];

wnd_dict_node_t
wnd_dict_find(xcb_window_t wnd, int op)
{
    int h = (unsigned int)wnd * HASH_MUL % HASH_MOD;
    wnd_dict_node_t last = NULL, node = wnd_hash_list[h];

    while (node != NULL)
    {
        if (node->wnd == wnd) break;
        
        last = node;
        node = node->next;
    }

    switch (op)
    {
    case WND_DICT_FIND_OP_TOUCH:
        node = (wnd_dict_node_t)malloc(sizeof(wnd_dict_node_s));
        node->wnd = wnd;
        node->role = WND_ROLE_INIT;
        node->link = NULL;

        node->next = wnd_hash_list[h];
        wnd_hash_list[h] = node;
        
        return node;

    case WND_DICT_FIND_OP_ERASE:
        if (node == NULL)
            return NULL;
        
        if (last == NULL)
        {
            wnd_hash_list[h] = node->next;
            free(node);
        }
        else
        {
            last->next = node->next;
            free(node);
        }
        return node;
        
    default:
        return node;
    }
}

#ifndef LASTEvent
#define LASTEvent 35
#endif

typedef void(*event_handler_t)(xcb_generic_event_t *e);

static void xcb_event_map_request(xcb_generic_event_t *e);
static void xcb_event_map_notify(xcb_generic_event_t *e);
static void xcb_event_unmap_notify(xcb_generic_event_t *e);
static void xcb_event_reparent_notify(xcb_generic_event_t *e);
static void xcb_event_destroy_notify(xcb_generic_event_t *e);
static void xcb_event_button_press(xcb_generic_event_t *e);
static void xcb_event_motion_notify(xcb_generic_event_t *e);
static void xcb_event_button_release(xcb_generic_event_t *e);

event_handler_t event_handlers[LASTEvent] =
{
    [XCB_MAP_REQUEST]     = xcb_event_map_request,
    [XCB_MAP_NOTIFY]      = xcb_event_map_notify,
    [XCB_UNMAP_NOTIFY]    = xcb_event_unmap_notify,
    [XCB_REPARENT_NOTIFY] = xcb_event_reparent_notify,
    [XCB_DESTROY_NOTIFY]  = xcb_event_destroy_notify,
    [XCB_BUTTON_PRESS]    = xcb_event_button_press,
    [XCB_MOTION_NOTIFY]   = xcb_event_motion_notify,
    [XCB_BUTTON_RELEASE]  = xcb_event_button_release,
};

xcb_connection_t *x_conn = NULL;
static int processing_flag = 1;

int      screen_count = 0;
screen_t screens = NULL;

static void __dcc_init(client_class_t self) { }
static const char *__dcc_class_name_get(client_class_t self) { return "DUMMY"; }
static int  __dcc_client_try_attach(client_class_t self, client_t client) { return CLIENT_TRY_ATTACH_FAILED; }

static client_class_s __dummy_client_class =
{
    .init                  = __dcc_init,
    .class_name_get        = __dcc_class_name_get,
    .client_try_attach     = __dcc_client_try_attach,
};

static client_t __client_attach(xcb_window_t window);
static void     __client_detach(client_t client, int forget);
static void     __client_map(client_t client);

#define DEFINE_ATOM(name) [name] = { XCB_NONE, #name, sizeof(#name) - 1 }
#define ATOM(name) atoms[name].atom

#define LENGTH(v) (sizeof(v) / sizeof((v)[0]))

#define _NET_WM_DESKTOP  0
#define WM_DELETE_WINDOW 1
#define WM_PROTOCOLS     2

static struct
{
    xcb_atom_t  atom;
    const char *name;
    const int   name_length;
} atoms[] = {
    DEFINE_ATOM(_NET_WM_DESKTOP),
    DEFINE_ATOM(WM_DELETE_WINDOW),
    DEFINE_ATOM(WM_PROTOCOLS),
};

static void
sigcatch(int signal)
{
    processing_flag = 0;
}

static int
__init(void)
{
    int ret = 0;
    int i;

    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
        return -1;

    if (signal(SIGINT, sigcatch) == SIG_ERR)
        return -1;

    if (signal(SIGTERM, sigcatch) == SIG_ERR)
        return -1;

    for (i = 0; i < HASH_MOD; ++ i) wnd_hash_list[i] = NULL;
        
    x_conn = xcb_connect(NULL, &screen_count);

    xcb_intern_atom_cookie_t atom_cookies[LENGTH(atoms)];

    for (i = 0; i < LENGTH(atoms); ++ i)
    {
        if (atoms[i].name == NULL) continue;
        atom_cookies[i] = xcb_intern_atom(x_conn, 0, atoms[i].name_length, atoms[i].name);
    }

    for (i = 0; i < LENGTH(atoms); ++ i)
    {
        if (atoms[i].name == NULL) continue;
        
        xcb_intern_atom_reply_t *r;
        r = xcb_intern_atom_reply(x_conn, atom_cookies[i], 0);
        if (r)
        {
            atoms[i].atom = r->atom;
            free(r);
        }
        else ret = -1;
    }

    if (ret)
    {
        printf("error while get atoms\n");
        return ret;
    }

    xcb_screen_iterator_t iter;
    int id;
    
    iter = xcb_setup_roots_iterator(xcb_get_setup(x_conn));
    for (id = 0; iter.rem; ++ id, xcb_screen_next (&iter)) ; screen_count = id;

    screens = (screen_t)malloc(screen_count * sizeof(screen_s));

    iter = xcb_setup_roots_iterator(xcb_get_setup(x_conn));
    for (id = 0; iter.rem; ++ id, xcb_screen_next (&iter))
    {
        screens[id].xcb_screen = iter.data;
        screens[id].mouse_attached = 0;
        screens[id].mouse_motion_callback = NULL;
        screens[id].mouse_release_callback = NULL;
        screens[id].mouse_cb_data = NULL;
        screens[id].focus = NULL;
        list_init(&screens[id].auto_scan_list);
        list_init(&screens[id].client_list);

        wnd_dict_node_t node = wnd_dict_find(screens[id].xcb_screen->root, WND_DICT_FIND_OP_TOUCH);
        node->role = WND_ROLE_ROOT;
        node->link = &screens[id];

        uint32_t mask = XCB_CW_EVENT_MASK;
        uint32_t values[1] = { XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
                               | XCB_EVENT_MASK_STRUCTURE_NOTIFY
                               | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY };
                              
        xcb_void_cookie_t cookie =
            xcb_change_window_attributes_checked(x_conn, screens[id].xcb_screen->root, mask, values);
        xcb_generic_error_t *error = xcb_request_check(x_conn, cookie);
        xcb_flush(x_conn);
    
        if (error != NULL)
        {
            fprintf(stderr, "Can't get SUBSTRUCTURE REDIRECT. "
                    "Error code: %d\n"
                    "Another window manager running?\n",
                    error->error_code);
            free(error);
            return -1;
        }
    }

    return 0;
}

static int
__setup(void)
{
    int i;
    for (i = 0; i < screen_count; ++ i)
    {
        
        /* scan all existing window */
        xcb_get_window_attributes_reply_t *attr;
        xcb_query_tree_reply_t *reply;
        
        reply = xcb_query_tree_reply(x_conn, xcb_query_tree(x_conn, screens[i].xcb_screen->root), 0);
        if (reply == NULL)
        {
            return -1;
        }

        int i, len = xcb_query_tree_children_length(reply);    
        xcb_window_t *children = xcb_query_tree_children(reply);
    
        for (i = 0; i < len; i ++)
        {
            attr = xcb_get_window_attributes_reply(
                x_conn, xcb_get_window_attributes(x_conn, children[i]), NULL);

            if (attr == NULL)
            {
                fprintf(stderr, "Couldn't get attributes for window %d.",
                        children[i]);
                continue;
            }

            /* Ignore windows with override_redirect, or windows that are
             * not viewable (we would manage then after the expose
             * event) */
            if (!attr->override_redirect)
            {
                client_t client = __client_attach(children[i]);
                if (attr->map_state == XCB_MAP_STATE_VIEWABLE)
                    __client_map(client);
            }
        
            free(attr);
        }

        xcb_flush(x_conn);
        free(reply);
    }

    return 0;
}

static void
xcb_event_map_request(xcb_generic_event_t *e)
{
    xcb_map_request_event_t *map_request = (xcb_map_request_event_t *)e;
    wnd_dict_node_t node = wnd_dict_find(map_request->window, WND_DICT_FIND_OP_TOUCH);

    switch (node->role)
    {
    case WND_ROLE_INIT:
    {
        client_t client = __client_attach(map_request->window);
        __client_map(client);
        break;
    }

    case WND_ROLE_CLIENT:
    {
        client_t client = (client_t)node->link;
        __client_map(client);
        break;
    }
        
    case WND_ROLE_CLIENT_IGNORE:
        break;
    }
}

static void
xcb_event_map_notify(xcb_generic_event_t *e)
{
    xcb_map_notify_event_t *map_notify = (xcb_map_notify_event_t *)e;
    wnd_dict_node_t node = wnd_dict_find(map_notify->window, WND_DICT_FIND_OP_NONE);

    if (node && node->role == WND_ROLE_CLIENT)
    {
        client_t client = (client_t)node->link;
        if (client->class && client->class->client_event_map_notify)
            client->class->client_event_map_notify(client->class, client, map_notify);
    }
}

static void
xcb_event_unmap_notify(xcb_generic_event_t *e)
{
    xcb_unmap_notify_event_t *unmap_notify = (xcb_unmap_notify_event_t *)e;
    wnd_dict_node_t node = wnd_dict_find(unmap_notify->window, WND_DICT_FIND_OP_NONE);

    if (node && node->role == WND_ROLE_CLIENT)
    {
        client_t client = (client_t)node->link;
        if (client->class && client->class->client_event_unmap_notify)
            client->class->client_event_unmap_notify(client->class, client, unmap_notify);
    }
}

static void
xcb_event_reparent_notify(xcb_generic_event_t *e)
{
    xcb_reparent_notify_event_t *reparent_notify = (xcb_reparent_notify_event_t *)e;

    if (reparent_notify->event == reparent_notify->parent) return;
    
    wnd_dict_node_t node = wnd_dict_find(reparent_notify->window, WND_DICT_FIND_OP_NONE);

    if (node == NULL ||
        (node->role != WND_ROLE_CLIENT &&
         node->role != WND_ROLE_CLIENT_IGNORE))
        return;

    client_t client = (client_t)node->link;

    switch (node->role)
    {
    case WND_ROLE_CLIENT:
        if (client->class && client->class->client_event_reparent_notify)
            client->class->client_event_reparent_notify(client->class, client, reparent_notify);
        break;
        
    case WND_ROLE_CLIENT_IGNORE:
        wnd_dict_find(reparent_notify->window, WND_DICT_FIND_OP_ERASE);
        break;
    }
}

static void
xcb_event_destroy_notify(xcb_generic_event_t *e)
{
    xcb_destroy_notify_event_t *destroy_notify = (xcb_destroy_notify_event_t *)e;
    wnd_dict_node_t node = wnd_dict_find(destroy_notify->window, WND_DICT_FIND_OP_NONE);

    if (node == NULL ||
        (node->role != WND_ROLE_CLIENT &&
         node->role != WND_ROLE_CLIENT_IGNORE))
        return;
    
    client_t client = (client_t)node->link;
    
    switch (node->role)
    {
    case WND_ROLE_CLIENT:
        __client_detach(client, 1);
        break;

    case WND_ROLE_CLIENT_IGNORE:
        wnd_dict_find(destroy_notify->window, WND_DICT_FIND_OP_ERASE);
        break;
    }
}

static void
xcb_event_button_press(xcb_generic_event_t *e)
{
    xcb_button_press_event_t *button_press = (xcb_button_press_event_t *)e;
    wnd_dict_node_t node = wnd_dict_find(button_press->event, WND_DICT_FIND_OP_NONE);
    int ret = CLIENT_INPUT_PASS_THROUGH;

    if (node && node->role == WND_ROLE_CLIENT)
    {
        client_t client = (client_t)node->link;
        if (client->class && client->class->client_event_button_press)
            ret = client->class->client_event_button_press(client->class, client, button_press);
    }

    if (ret == CLIENT_INPUT_PASS_THROUGH)
        xcb_allow_events(x_conn, XCB_ALLOW_REPLAY_POINTER, button_press->time);
}

static void
xcb_event_motion_notify(xcb_generic_event_t *e)
{
    xcb_motion_notify_event_t *motion_notify = (xcb_motion_notify_event_t *)e;
    wnd_dict_node_t node = wnd_dict_find(motion_notify->root, WND_DICT_FIND_OP_NONE);

    if (node == NULL)
        return;

    screen_t screen = (screen_t)node->link;
    xcb_query_pointer_reply_t *pointer;
    pointer = xcb_query_pointer_reply(x_conn, xcb_query_pointer(x_conn, screen->xcb_screen->root), 0);

    if (screen->mouse_attached && screen->mouse_motion_callback != NULL)
        screen->mouse_motion_callback(screen->mouse_cb_data, pointer->root_x, pointer->root_y);

    free(pointer);
}

static void
xcb_event_button_release(xcb_generic_event_t *e)
{
    xcb_button_release_event_t *button_release = (xcb_button_release_event_t *)e;
    wnd_dict_node_t node = wnd_dict_find(button_release->root, WND_DICT_FIND_OP_NONE);
    
    if (node == NULL)
        return;

    screen_t screen = (screen_t)node->link;

    if (screen->mouse_attached && screen->mouse_release_callback != NULL)
        screen->mouse_release_callback(screen->mouse_cb_data);
}

static void
__event_loop(void)
{
    xcb_generic_event_t *e;
    /* Use select to get signal, learnt from MCWM */    
    int fd;
    fd_set in;

    fd = xcb_get_file_descriptor(x_conn);
    FD_ZERO(&in);
    FD_SET(fd, &in);
    
    while (processing_flag && !xcb_connection_has_error(x_conn))
    {
        e = xcb_poll_for_event(x_conn);
        if (e == NULL)
        {
            select(fd + 1, &in, NULL, NULL, NULL);
            continue;
        }

        event_handler_t h = event_handlers[e->response_type & ~0x80];
        if (h) h(e);

        free(e);
        xcb_flush(x_conn);
    }
}

static int
__cleanup(void)
{
    if (screens && !xcb_connection_has_error(x_conn))
    {
        /* Detach all clients */
        int i;
        list_entry_t cur;
        for (i = 0; i < screen_count; ++ i)
        {
            cur = list_next(&screens[i].client_list);
            while (cur != &screens[i].client_list)
            {
                client_t client = CONTAINER_OF(cur, client_s, client_node);
                __client_detach(client, 1);

                cur = list_next(cur);
            }
            xcb_flush(x_conn);
        }
    }

    if (x_conn)
        xcb_disconnect(x_conn);
    
    return 0;    
}

static client_t
__client_attach(xcb_window_t window)
{
    wnd_dict_node_t node = wnd_dict_find(window, WND_DICT_FIND_OP_NONE);
    if (node && node->role == WND_ROLE_CLIENT_IGNORE)
    {
        /* Skip attached window */
        return NULL;
    }
    
    xcb_window_t parent;
    xh_window_geom_get(window, &parent, NULL);

    node = wnd_dict_find(parent, WND_DICT_FIND_OP_NONE);
    if (node == NULL || node->role != WND_ROLE_ROOT)
        return NULL;

    screen_t screen = (screen_t)node->link;

    client_t client = (client_t)malloc(sizeof(client_s));
    
    client->screen = screen;
    client->xcb_window = window;
    list_add(&screen->client_list, &client->client_node);

    node = wnd_dict_find(window, WND_DICT_FIND_OP_TOUCH);
    node->role = WND_ROLE_CLIENT;
    node->link = client;

    client->class = &__dummy_client_class;

    list_entry_t cur = list_next(&screen->auto_scan_list);
    while (cur != &screen->auto_scan_list)
    {
        client_class_t class = CONTAINER_OF(cur, client_class_s, auto_scan_node);
        if (class->client_try_attach(class, client) == CLIENT_TRY_ATTACH_ATTACHED)
            break;
        cur = list_next(cur);
    }

    DEBUGP("client: %08x attached to class: %s\n", window, client->class->class_name_get(client->class));
    
    return client;
}

static void
__client_detach(client_t client, int forget)
{
    if (client->class && client->class->client_detach)
        client->class->client_detach(client->class, client, !forget);

    if (forget)
    {
        wnd_dict_find(client->xcb_window, WND_DICT_FIND_OP_ERASE);
        xcb_unmap_window(x_conn, client->xcb_window);
    }
    else
    {
        wnd_dict_node_t node;
        node = wnd_dict_find(client->xcb_window, WND_DICT_FIND_OP_NONE);
        node->role = WND_ROLE_CLIENT_IGNORE;
        node->link = NULL;
    }

    if (client->screen->focus == client)
        client->screen->focus = NULL;

    list_del(&client->client_node);
    free(client);
}

static void
__client_map(client_t client)
{
    if (client->class && client->class->client_map)
        client->class->client_map(client->class, client);
}

void
client_class_auto_scan_attach(screen_t screen, client_class_t cc)
{
    list_add(&screen->auto_scan_list, &cc->auto_scan_node);
}

void
client_class_auto_scan_detach(screen_t screen, client_class_t cc)
{
    list_del(&cc->auto_scan_node);
}

int
screen_mouse_attach(screen_t screen, mouse_motion_callback_f motion_callback, mouse_release_callback_f release_callback, void *data)
{
    if (screen->mouse_attached)
        return SCREEN_MOUSE_POINTER_ATTACH_FAILED;

    screen->mouse_motion_callback = motion_callback;
    screen->mouse_release_callback = release_callback;
    screen->mouse_cb_data = data;
    screen->mouse_attached = 1;

    xcb_grab_pointer(x_conn, 0, screen->xcb_screen->root,
                     XCB_EVENT_MASK_BUTTON_RELEASE
                     | XCB_EVENT_MASK_BUTTON_MOTION
                     | XCB_EVENT_MASK_POINTER_MOTION_HINT,
                     XCB_GRAB_MODE_ASYNC,
                     XCB_GRAB_MODE_ASYNC,
                     screen->xcb_screen->root,
                     XCB_NONE,
                     XCB_CURRENT_TIME);

    return SCREEN_MOUSE_POINTER_ATTACH_ATTACHED;
}

void
screen_mouse_detach(screen_t screen)
{
    if (screen->mouse_attached == 0)
        return;

    screen->mouse_attached = 0;
    xcb_ungrab_pointer(x_conn, XCB_CURRENT_TIME);    
}

int
xh_window_geom_get(xcb_window_t window, xcb_window_t *parent, rect_t rect)
{
    xcb_query_tree_cookie_t   tree_cookie;
    xcb_query_tree_reply_t   *tree;

    xcb_get_geometry_cookie_t  geom_cookie;
    xcb_get_geometry_reply_t  *geom;

    if (parent) tree_cookie = xcb_query_tree(x_conn, window);
    if (rect)   geom_cookie = xcb_get_geometry(x_conn, window);
    
    if (parent)
    {
        if ((tree = xcb_query_tree_reply(x_conn, tree_cookie, NULL)) == NULL)
        {
            /* XXX need to stop geom query? */
            return -1;
        }
        *parent = tree->parent;
        free(tree);
    }

    if (rect)
    {
        if ((geom = xcb_get_geometry_reply(x_conn, geom_cookie, NULL)) == NULL)
            return -1;
        rect->x = geom->x;
        rect->y = geom->y;
        rect->w = geom->width;
        rect->h = geom->height;
        free(geom);
    }

    return 0;
}

void
focus_set(client_t client)
{
    client_t old = client->screen->focus;
    if (old == client) return;

    if (old != NULL)
    {
        if (old->class && old->class->client_aevent_blur)
            old->class->client_aevent_blur(old->class, old);
    }

    if (client->class && client->class->client_aevent_focus)
        client->class->client_aevent_focus(client->class, client);

    xcb_set_input_focus(x_conn, XCB_INPUT_FOCUS_POINTER_ROOT, client->xcb_window, XCB_CURRENT_TIME);
    client->screen->focus = client;
}

int
main(void)
{
    int ret;
    
    ret = __init();
    if (ret == 0)
    {
        cc_simple->init(cc_simple);
        __setup();
    }
    if (ret == 0) __event_loop();
    ret = __cleanup();

    return ret;
}

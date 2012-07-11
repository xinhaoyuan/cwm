#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "structure.h"

#define HASH_MOD 19997
#define HASH_MUL 10007

#define ROLE_INIT             0
#define ROLE_ROOT             1
#define ROLE_CLIENT_CONTAINER 2
#define ROLE_CLIENT_ORIG      3
#define ROLE_CLIENT_IGNORE    4

static struct wnd_dict_node_s
{
    xcb_window_t wnd;
    int          role;
    void        *link;
    struct wnd_dict_node_s *next;
} *wnd_hash_list[HASH_MOD];

typedef struct wnd_dict_node_s wnd_dict_node_s;
typedef wnd_dict_node_s *wnd_dict_node_t;

#define WND_DICT_FIND_OP_NONE  0
#define WND_DICT_FIND_OP_TOUCH 1
#define WND_DICT_FIND_OP_ERASE 2

static wnd_dict_node_t
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
        node->role = ROLE_INIT;
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
static void xcb_event_button_press(xcb_generic_event_t *e);
static void xcb_event_motion_notify(xcb_generic_event_t *e);
static void xcb_event_button_release(xcb_generic_event_t *e);

event_handler_t event_handlers[LASTEvent] =
{
    [XCB_MAP_REQUEST]    = xcb_event_map_request,
    [XCB_BUTTON_PRESS]   = xcb_event_button_press,
    [XCB_MOTION_NOTIFY]  = xcb_event_motion_notify,
    [XCB_BUTTON_RELEASE] = xcb_event_button_release,
};

static xcb_connection_t *x_conn = NULL;
static int screen_count = 0;
static screen_t screens = NULL;

#define DYN_STRING(string_const)                                        \
    ({ char *r = (char *)malloc(sizeof(string_const));                  \
        if (r) memcpy(r, string_const, sizeof(string_const)); r; })

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

static int
get_geom(xcb_window_t window, xcb_window_t *parent, rect_t rect)
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

static int
init(void)
{
    int ret = 0;
    int i;

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
        screens[id].inactive_border_pixel = screens[id].xcb_screen->black_pixel;
        screens[id].active_border_pixel = screens[id].xcb_screen->white_pixel;

        screens[id].mode = SCREEN_MODE_NORMAL;
        screens[id].focus = NULL;
        
        list_init(&screens[id].clients);

        wnd_dict_node_t node = wnd_dict_find(screens[id].xcb_screen->root, WND_DICT_FIND_OP_TOUCH);
        node->role = ROLE_ROOT;
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
setup(void)
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
            if (!attr->override_redirect && attr->map_state == XCB_MAP_STATE_VIEWABLE)
            {
                client_attach(children[i]);
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
    case ROLE_INIT:
    {
        client_attach(map_request->window);
        break;
    }

    case ROLE_CLIENT_CONTAINER:
        break;

    case ROLE_CLIENT_ORIG:
        break;
    }
}

static void
xcb_event_button_press(xcb_generic_event_t *e)
{
    xcb_button_press_event_t *button_press = (xcb_button_press_event_t *)e;
    wnd_dict_node_t node = wnd_dict_find(button_press->event, WND_DICT_FIND_OP_NONE);
    if (node == NULL)
        goto pass_through_bp;

    switch (node->role)
    {
    case ROLE_CLIENT_CONTAINER:
    {
        client_t client = (client_t)node->link;
        screen_t screen = client->screen;

        client_focus(client);

        if ((button_press->state & XCB_MOD_MASK_1) &&
            screen->mode == SCREEN_MODE_NORMAL)
        {
            xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply(x_conn, xcb_get_geometry(x_conn, client->xcb_container), NULL);
            if (button_press->detail == XCB_BUTTON_INDEX_1)
            {
                screen->mode_pointer_x = geom->x - button_press->root_x;
                screen->mode_pointer_y = geom->y - button_press->root_y;
                screen->mode = SCREEN_MODE_MOVE_WINDOW_BY_MOUSE;
            }
            else
            {
                screen->mode_pointer_x = geom->width - button_press->root_x;
                screen->mode_pointer_y = geom->height - button_press->root_y;
                screen->mode = SCREEN_MODE_RESIZE_WINDOW_BY_MOUSE;
            }
            free(geom);
            
            xcb_grab_pointer(x_conn, 0, screen->xcb_screen->root,
                             XCB_EVENT_MASK_BUTTON_RELEASE
                             | XCB_EVENT_MASK_BUTTON_MOTION
                             | XCB_EVENT_MASK_POINTER_MOTION_HINT,
                             XCB_GRAB_MODE_ASYNC,
                             XCB_GRAB_MODE_ASYNC,
                             screen->xcb_screen->root,
                             XCB_NONE,
                             XCB_CURRENT_TIME);
                    
            xcb_allow_events(x_conn, XCB_ALLOW_SYNC_POINTER, button_press->time);
        }
        else goto pass_through_bp;
        break;
    }
    default:
        goto pass_through_bp;
    }

  pass_through_bp:
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

    switch (screen->mode)
    {
    case SCREEN_MODE_MOVE_WINDOW_BY_MOUSE:
    {
        client_t client = screen->focus;
        uint32_t values[2];
        values[0] = screen->mode_pointer_x + pointer->root_x;
        values[1] = screen->mode_pointer_y + pointer->root_y;
        xcb_configure_window(x_conn, client->xcb_container,
                             XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
        break;
    }

    case SCREEN_MODE_RESIZE_WINDOW_BY_MOUSE:
    {
        client_t client = screen->focus;
        uint32_t values[2];
        int w = screen->mode_pointer_x + pointer->root_x;
        int h = screen->mode_pointer_y + pointer->root_y;
        values[0] = w < 32 ? 32 : w;
        values[1] = h < 32 ? 32 : h;
        xcb_configure_window(x_conn, client->xcb_container,
                             XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
        break;
    }
            
    default: break;
    }

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
    switch (screen->mode)
    {
    case SCREEN_MODE_RESIZE_WINDOW_BY_MOUSE:
    {
        xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply(x_conn, xcb_get_geometry(x_conn, screen->focus->xcb_container), NULL);
        uint32_t values[2] = { geom->width, geom->height };

        xcb_configure_window(x_conn, screen->focus->xcb_orig,
                             XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);

        free(geom);
        /* no break, same as move window mode */
    }
    case SCREEN_MODE_MOVE_WINDOW_BY_MOUSE:
    {
        screen->mode = SCREEN_MODE_NORMAL;
        xcb_ungrab_pointer(x_conn, XCB_CURRENT_TIME);
        break;
    }
            
    default: break;
    }
}

static void
eventloop(void)
{
    xcb_generic_event_t *e;
    while (1)
    {
        e = xcb_wait_for_event(x_conn);
        if (e == NULL) continue;

        event_handler_t h = event_handlers[e->response_type & ~0x80];
        if (h) h(e);

        free(e);
        xcb_flush(x_conn);
    }
}

static int
cleanup(void)
{
    if (screens)
        free(screens);
    if (x_conn)
        xcb_disconnect(x_conn);
    
    return 0;    
}

client_t
client_attach(xcb_window_t window)
{
    xcb_window_t parent;
    rect_s       geom;
    
    get_geom(window, &parent, &geom);

    wnd_dict_node_t node = wnd_dict_find(parent, WND_DICT_FIND_OP_NONE);
    if (node == NULL || node->role != ROLE_ROOT)
        return NULL;

    screen_t screen = (screen_t)node->link;

    client_t result = (client_t)malloc(sizeof(client_s));
    
    result->screen = screen;
    result->xcb_container = XCB_NONE;
    result->xcb_orig = window;
    result->geom_state = GEOM_STATE_NORMAL;
    result->mapped = 0;
    
    list_add(&screen->clients, &result->client_node);

    /* create a container with border */
    
    xcb_window_t cont = xcb_generate_id(x_conn);
    uint32_t mask     = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    uint32_t values[] = { 1,
                          XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                          XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                          XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY };
    
    xcb_create_window(x_conn,
                      XCB_COPY_FROM_PARENT,
                      cont,
                      parent,
                      geom.x, geom.y,
                      geom.w, geom.h,
                      1,        /* border width */
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->xcb_screen->root_visual,
                      mask, values);
    result->xcb_container = cont;
    xcb_reparent_window(x_conn, window, cont, 0, 0);

    values[0] = screen->inactive_border_pixel;
    xcb_change_window_attributes(x_conn, cont, XCB_CW_BORDER_PIXEL, values);

    xcb_grab_button(x_conn, 0, cont, XCB_EVENT_MASK_BUTTON_PRESS,
                    XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE,
                    XCB_BUTTON_INDEX_1, XCB_MOD_MASK_ANY);

    xcb_grab_button(x_conn, 0, cont, XCB_EVENT_MASK_BUTTON_PRESS,
                    XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE,
                    XCB_BUTTON_INDEX_3, XCB_MOD_MASK_ANY);

    node = wnd_dict_find(window, WND_DICT_FIND_OP_TOUCH);
    node->role = ROLE_CLIENT_ORIG;
    node->link = result;

    node = wnd_dict_find(cont, WND_DICT_FIND_OP_TOUCH);
    node->role = ROLE_CLIENT_CONTAINER;
    node->link = result;

    client_map(result);

    return result;
}

void
client_detach(client_t client)
{
    rect_s geom;
    get_geom(client->xcb_container, NULL, &geom);
    xcb_reparent_window(x_conn, client->xcb_orig, client->screen->xcb_screen->root, geom.x, geom.y);
    
    list_del(&client->client_node);
    
    wnd_dict_node_t node = wnd_dict_find(client->xcb_container, WND_DICT_FIND_OP_ERASE);
    node = wnd_dict_find(client->xcb_orig, WND_DICT_FIND_OP_NONE);

    node->role = ROLE_CLIENT_IGNORE;
    node->link = NULL;

    if (client->mapped)
        xcb_map_window(x_conn, client->xcb_orig);

    client_unmap(client);
    free(client);
}

void
client_focus(client_t client)
{
    uint32_t values[1];
    
    client_t old = client->screen->focus;
    if (old == client) return;

    if (old != NULL)
    {
        values[0] = client->screen->inactive_border_pixel;
        xcb_change_window_attributes(x_conn, old->xcb_container, XCB_CW_BORDER_PIXEL, values);
    }

    values[0] = client->screen->active_border_pixel;
    xcb_change_window_attributes(x_conn, client->xcb_container, XCB_CW_BORDER_PIXEL, values);
    xcb_set_input_focus(x_conn, XCB_INPUT_FOCUS_POINTER_ROOT, client->xcb_orig, XCB_CURRENT_TIME);

    values[0] = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(x_conn, client->xcb_container, XCB_CONFIG_WINDOW_STACK_MODE, values);

    client->screen->focus = client;
}

void
client_map(client_t client)
{
    if (client->mapped) return;
    client->mapped = 1;
    
    xcb_map_window(x_conn, client->xcb_container);
}

void
client_unmap(client_t client)
{
    if (client->mapped == 0) return;
    client->mapped = 0;
    
    if (client->screen->focus == client)
    {
        uint32_t values[1];

        values[0] = client->screen->inactive_border_pixel;
        xcb_change_window_attributes(x_conn, client->xcb_container, XCB_CW_BORDER_PIXEL, values);

        client->screen->focus = NULL;
    }
    
    xcb_unmap_window(x_conn, client->xcb_container);
}

int
main(int argc, char *argv[])
{
    int ret;

    ret = init();
    if (ret == 0) ret = setup();
    if (ret == 0) eventloop();
    cleanup();

    return ret;
}

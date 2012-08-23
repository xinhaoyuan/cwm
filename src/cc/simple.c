#include "../base.h"
#include "simple.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct cc_simple_priv_s *cc_simple_priv_t;
typedef struct cc_simple_priv_s
{
    xcb_window_t xcb_container;
    int mapped;
} cc_simple_priv_s;

typedef struct cc_simple_data_s *cc_simple_data_t;
typedef struct cc_simple_data_s
{
    client_class_s interface;

    uint32_t inactive_border_color;
    uint32_t active_border_color;
    
    int      mouse_mode;
    int      mouse_mode_x;
    int      mouse_mode_y;
    client_t mouse_mode_client;
} cc_simple_data_s;

#define MOUSE_MODE_NORMAL                 0
#define MOUSE_MODE_MOVE_WINDOW_BY_MOUSE   1
#define MOUSE_MODE_RESIZE_WINDOW_BY_MOUSE 2

static void
scc_init(client_class_t self)
{
    cc_simple_data_t data = (cc_simple_data_t)self;
    data->inactive_border_color = screens[0].xcb_screen->black_pixel;
    data->active_border_color   = screens[0].xcb_screen->white_pixel;
    data->mouse_mode = MOUSE_MODE_NORMAL;
    int i;
    for (i = 0; i < screen_count; ++ i)
        client_class_auto_scan_attach(&screens[i], self);
}

static const char *
scc_class_name_get(client_class_t self)
{ return "SimpleClientClass"; }

static int
scc_client_try_attach(client_class_t self, client_t client)
{
    cc_simple_data_t data = (cc_simple_data_t)self;
    cc_simple_priv_t priv = malloc(sizeof(cc_simple_priv_s));
    if (priv == NULL)
        return CLIENT_TRY_ATTACH_FAILED;
    
    rect_s geom;
    client->priv = priv;
    client->class = self;

    xh_window_geom_get(client->xcb_window, NULL, &geom);

    priv->mapped = 0;
    priv->xcb_container = xcb_generate_id(x_conn);
    uint32_t mask       = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    uint32_t values[]   = { 1,
                            XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                            XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                            XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY };
    
    xcb_create_window(x_conn,
                      XCB_COPY_FROM_PARENT,
                      priv->xcb_container,
                      client->screen->xcb_screen->root,
                      geom.x, geom.y,
                      geom.w, geom.h,
                      1,        /* border width */
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      client->screen->xcb_screen->root_visual,
                      mask, values);
    xcb_reparent_window(x_conn, client->xcb_window, priv->xcb_container, 0, 0);
    xcb_map_window(x_conn, client->xcb_window);

    xcb_grab_button(x_conn, 0, priv->xcb_container, XCB_EVENT_MASK_BUTTON_PRESS,
                    XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE,
                    XCB_BUTTON_INDEX_1, XCB_MOD_MASK_ANY);

    xcb_grab_button(x_conn, 0, priv->xcb_container, XCB_EVENT_MASK_BUTTON_PRESS,
                    XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE,
                    XCB_BUTTON_INDEX_3, XCB_MOD_MASK_ANY);

    values[0] = data->inactive_border_color;
    xcb_change_window_attributes(x_conn, priv->xcb_container, XCB_CW_BORDER_PIXEL, values);

    wnd_dict_node_t node = wnd_dict_find(priv->xcb_container, WND_DICT_FIND_OP_TOUCH);
    node->role = WND_ROLE_CLIENT;
    node->link = client;
    
    return CLIENT_TRY_ATTACH_ATTACHED;
}

static void
scc_client_map(client_class_t self, client_t client)
{
    cc_simple_priv_t priv = client->priv;
    if (priv->mapped) return;
    priv->mapped = 1;
    
    xcb_map_window(x_conn, priv->xcb_container);
}

static void
scc_client_unmap(client_class_t self, client_t client)
{
    cc_simple_priv_t priv = client->priv;
    if (priv->mapped == 0) return;
    priv->mapped = 0;
    
    xcb_unmap_window(x_conn, priv->xcb_container);
}

static void scc_mouse_release_callback(void *__data);

static void
scc_client_detach(client_class_t self, client_t client, int keep_mapped)
{
    cc_simple_data_t data = (cc_simple_data_t)self;
    cc_simple_priv_t priv = client->priv;
    rect_s geom;

    if (data->mouse_mode != MOUSE_MODE_NORMAL && data->mouse_mode_client == client)
    {
        scc_mouse_release_callback(data);
    }
    
    xh_window_geom_get(priv->xcb_container, NULL, &geom);
    xcb_reparent_window(x_conn, client->xcb_window, client->screen->xcb_screen->root, geom.x, geom.y);

    int m = priv->mapped;
    scc_client_unmap(self, client);
    
    wnd_dict_find(priv->xcb_container, WND_DICT_FIND_OP_ERASE);
    if (m && keep_mapped) xcb_map_window(x_conn, client->xcb_window);
}

static void
scc_mouse_motion_callback(void *__data, int abs_x, int abs_y)
{
    cc_simple_data_t data = (cc_simple_data_t)__data;
    client_t client = data->mouse_mode_client;
    cc_simple_priv_t priv = client->priv;

    switch (data->mouse_mode)
    {
    case MOUSE_MODE_MOVE_WINDOW_BY_MOUSE:
    {
        uint32_t values[2];
        values[0] = data->mouse_mode_x + abs_x;
        values[1] = data->mouse_mode_y + abs_y;
        xcb_configure_window(x_conn, priv->xcb_container,
                             XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
        break;
    }

    case MOUSE_MODE_RESIZE_WINDOW_BY_MOUSE:
    {
        uint32_t values[2];
        int w = data->mouse_mode_x + abs_x;
        int h = data->mouse_mode_y + abs_y;
        values[0] = w < 32 ? 32 : w;
        values[1] = h < 32 ? 32 : h;
        xcb_configure_window(x_conn, priv->xcb_container,
                             XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
        break;
    }
    
    default: break;
    }
}

static void
scc_mouse_release_callback(void *__data)
{
    cc_simple_data_t data = (cc_simple_data_t)__data;
    client_t client = data->mouse_mode_client;
    cc_simple_priv_t priv = client->priv;

    switch (data->mouse_mode)
    {
    case MOUSE_MODE_RESIZE_WINDOW_BY_MOUSE:
    {
        xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply(x_conn, xcb_get_geometry(x_conn, priv->xcb_container), NULL);
        uint32_t values[2] = { geom->width, geom->height };

        xcb_configure_window(x_conn, client->xcb_window,
                             XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);

        free(geom);
        /* no break, same as move window mode */
    }
    
    case MOUSE_MODE_MOVE_WINDOW_BY_MOUSE:
    {
        data->mouse_mode = MOUSE_MODE_NORMAL;
        screen_mouse_detach(client->screen);
        break;
    }
    
    default: break;
    }
}

static int
scc_client_event_button_press(client_class_t self, client_t client, xcb_button_press_event_t *button_press)
{
    cc_simple_data_t data = (cc_simple_data_t)self;
    cc_simple_priv_t priv = client->priv;

    focus_set(client);
    
    if ((button_press->state & XCB_MOD_MASK_1))
    {
        int mode = button_press->detail == XCB_BUTTON_INDEX_1 ?
            MOUSE_MODE_MOVE_WINDOW_BY_MOUSE : MOUSE_MODE_RESIZE_WINDOW_BY_MOUSE;

        data->mouse_mode        = mode;
        data->mouse_mode_client = client;
        
        xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply(x_conn, xcb_get_geometry(x_conn, priv->xcb_container), NULL);
        switch (mode)
        {
        case MOUSE_MODE_MOVE_WINDOW_BY_MOUSE:
            data->mouse_mode_x = geom->x - button_press->root_x;
            data->mouse_mode_y = geom->y - button_press->root_y;
            break;

        case MOUSE_MODE_RESIZE_WINDOW_BY_MOUSE:
            data->mouse_mode_x = geom->width - button_press->root_x;
            data->mouse_mode_y = geom->height - button_press->root_y;
            break;
        }
        free(geom);
        
        screen_mouse_attach(client->screen, scc_mouse_motion_callback, scc_mouse_release_callback, data);
        xcb_allow_events(x_conn, XCB_ALLOW_SYNC_POINTER, button_press->time);
        
        return CLIENT_INPUT_CATCHED;
    }
    return CLIENT_INPUT_PASS_THROUGH;
}

static void
scc_client_event_map_notify(client_class_t self, client_t client, xcb_map_notify_event_t *e)
{ }

static void
scc_client_event_unmap_notify(client_class_t self, client_t client, xcb_unmap_notify_event_t *e)
{ }

static void
scc_client_event_reparent_notify(client_class_t self, client_t client, xcb_reparent_notify_event_t *e)
{ }

static void
scc_client_aevent_focus(client_class_t self, client_t client)
{
    uint32_t values[1];
    cc_simple_data_t data = (cc_simple_data_t)self;
    cc_simple_priv_t priv = client->priv;
    
    values[0] = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(x_conn, priv->xcb_container, XCB_CONFIG_WINDOW_STACK_MODE, values);

    values[0] = data->active_border_color;
    xcb_change_window_attributes(x_conn, priv->xcb_container, XCB_CW_BORDER_PIXEL, values);
}

static void
scc_client_aevent_blur(client_class_t self, client_t client)
{
    uint32_t values[1];
    cc_simple_data_t data = (cc_simple_data_t)self;
    cc_simple_priv_t priv = client->priv;

    values[0] = data->inactive_border_color;
    xcb_change_window_attributes(x_conn, priv->xcb_container, XCB_CW_BORDER_PIXEL, values);
}

cc_simple_data_s __cc_simple = 
{
    .interface = 
    {
        .init                         = scc_init,
        .class_name_get               = scc_class_name_get,
        .client_try_attach            = scc_client_try_attach,
        .client_map                   = scc_client_map,
        .client_unmap                 = scc_client_unmap,
        .client_detach                = scc_client_detach,
        .client_event_button_press    = scc_client_event_button_press,
        .client_event_map_notify      = scc_client_event_map_notify,
        .client_event_unmap_notify    = scc_client_event_unmap_notify,
        .client_event_reparent_notify = scc_client_event_reparent_notify,
        .client_aevent_focus          = scc_client_aevent_focus,
        .client_aevent_blur           = scc_client_aevent_blur,
    },
};

client_class_t cc_simple = (client_class_t)&__cc_simple;

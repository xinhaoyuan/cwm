#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vm/interp.h>

#include "base.h"
#include "engine.h"

interp_s interp;
object_t event_handler;

#define SYMBOL_EVENT_INIT SYMBOL_BOX(OBJECT_ID_UD_START + 0)

struct simple_stream
{
    FILE *file;
    int   buf;
};

#define BUF_EMPTY (-2)

int simple_stream_in(struct simple_stream *stream, int advance)
{
    int r;
    if (advance)
    {
        if (stream->buf == BUF_EMPTY)
            r = fgetc(stream->file);
        else
        {
            r = stream->buf;
            stream->buf = BUF_EMPTY;
        }
    }
    else
    {
        if (stream->buf == BUF_EMPTY)
        {
            stream->buf = fgetc(stream->file);
            if (stream->buf < 0) stream->buf = -1;
        }
        
        r = stream->buf;
    }
    return r;
}

static object_t
see_string_create(interp_t interp, const char *str, unsigned int len)
{
    object_t result = interp_object_new(interp);
    result->string = xstring_from_cstr(str, len);
    OBJECT_TYPE_INIT(result, OBJECT_TYPE_STRING);

    return result;
}

#define SEE_STRING(str) (see_string_create(&interp, str, sizeof(str)))

static object_t
interp_run_no_escape(interp_t interp)
{
    object_t  result;
    int       ex_argc = 0;
    object_t *ex_args;
    object_t  ex_ret = NULL;
    int i;
        
    while (1)
    {
        int r = interp_run(interp, ex_ret, &ex_argc, &ex_args);

        switch (r)
        {
        case APPLY_EXTERNAL_CALL:
            break;
            
        case APPLY_EXIT:
            if (ex_argc == 0)
            {
                result = NULL;
            }
            else
            {
                for (i = 1; i < ex_argc; ++ i)
                    interp_unprotect(interp, ex_args[i]);
                result = ex_args[0];
            }
            goto exit;

        default:
            result = NULL;
            goto exit;
        }
            
        if (xstring_equal_cstr(ex_args[0]->string, "debug", -1))
        {
            for (i = 1; i != ex_argc; ++ i)
            {
                object_dump(ex_args[i], stderr);
            }
            fprintf(stderr, "\n");
            ex_ret = OBJECT_NULL;
        }
        else ex_ret = OBJECT_NULL;

        /* The caller should unprotect the ex arguments by themself */
        for (i = 0; i != ex_argc; ++ i)
            interp_unprotect(interp, ex_args[i]);
    }

  exit:
    
#if 0
    fprintf(stderr, "return:");
    object_dump(result, stderr);
    fprintf(stderr, "\n");
#endif
    
    return result;
}

struct hook_event_s
{
    unsigned int argc;
    object_t    *args;
    
    list_entry_s node;
};

typedef struct hook_event_s  hook_event_s;
typedef struct hook_event_s *hook_event_t;

static list_entry_s hook_events;

static void
event_push(unsigned int argc, const object_t *args)
{
    hook_event_t he = malloc(sizeof(hook_event_s));
    if ((he->argc = argc) == 0)
        he->args = NULL;
    else
    {
        he->args = malloc(sizeof(object_t) * argc);
        memcpy(he->args, args, sizeof(object_t) * argc);
    }
    
    list_add_before(&hook_events, &he->node);
}

static hook_event_t
event_pop(void)
{
    if (list_empty(&hook_events)) return NULL;
    
    list_entry_t cur = list_next(&hook_events);
    list_del(cur);

    return CONTAINER_OF(cur, hook_event_s, node);
}

static void
event_free(hook_event_t he)
{
    if (he->args) free(he->args);
    free(he);
}

int
hook_init(void)
{
    list_init(&hook_events);
    
    interp_initialize(&interp, 16);
    event_handler = OBJECT_NULL;
    
    struct simple_stream s;
    char *rc_fn;
    const char *rc_env = getenv("CWM_RC");
    
    if (rc_env == NULL)
    {
        const char *home = getenv("HOME");
        if (home == NULL)
            rc_fn = DYN_STRING(CWM_RC);
        else {
            size_t l = strlen(home);
            rc_fn = malloc(l + sizeof("/" CWM_RC) + 1);
            memcpy(rc_fn, home, l);
            memcpy(rc_fn + l, "/" CWM_RC, sizeof("/" CWM_RC));
            rc_fn[l + sizeof("/" CWM_RC)] = '\0';
        }
    }
    else
    {
        size_t l = strlen(rc_env);
        rc_fn = malloc(l + 1);
        memcpy(rc_fn, rc_env, l);
        rc_fn[l] = '\0';
    }

    fprintf(stderr, "Use rc file %s\n", rc_fn);
    s.file = fopen(rc_fn, "r");
    free(rc_fn);
    
    if (s.file == NULL) return -1;
    s.buf  = BUF_EMPTY;

    object_t prog = interp_eval(&interp, (stream_in_f)simple_stream_in, &s);

    fclose(s.file);

    interp_apply(&interp, prog, 0, NULL);
    if ((event_handler = interp_run_no_escape(&interp))
        == NULL) return -1;

    event_push(1, (const object_t[]){ SYMBOL_EVENT_INIT });

    return 0;
}

void
hook_before_external_event(void)
{
    hook_event_t he;
    while ((he = event_pop()) != NULL)
    {
        interp_apply(&interp, event_handler, he->argc, he->args);
        event_free(he);
        interp_run_no_escape(&interp);
    }
}

void
hook_client_created(client_t client)
{
    client_map(client);
}

void
hook_client_orig_mapped(client_t client)
{
}

void
hook_client_orig_unmapped(client_t client)
{
    client_unmap(client);
}

void
hook_client_before_destroy(client_t client)
{
}

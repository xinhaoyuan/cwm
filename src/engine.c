#include <stdio.h>

#include "base.h"
#include "engine.h"

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

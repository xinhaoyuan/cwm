#ifndef __WM_ENGINE_H__
#define __WM_ENGINE_H__

void hook_client_created(client_t client);
void hook_client_orig_mapped(client_t client);
void hook_client_orig_unmapped(client_t client);
void hook_client_before_destroy(client_t client);

#endif

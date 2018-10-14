#pragma once


struct MallocHooks {
    void* old_malloc_hook;
};

void mallocHook_init(struct MallocHooks* hooks);
void mallocHook_kill(struct MallocHooks* hooks);

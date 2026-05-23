#include "ds.h"
#include <cassert>
#include <cstring>

int main() {
    assert(ds_status_name(DS_OK) != nullptr);

    ds_str_t *s = str_create();
    assert(s != nullptr);
    assert(str_append_cstr(s, "abc") == 0);
    assert(std::strcmp(FUNC_str_cstr(s), "abc") == 0);
    str_destroy(s);

    ds_context_t context;
    ds_context_init(&context);

    ds_arena_t arena;
    assert(ds_arena_create(&arena, 1024, &context) == DS_OK);
    ds_arena_destroy(&arena, &context);

    return 0;
}

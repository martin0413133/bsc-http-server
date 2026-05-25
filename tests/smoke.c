#include <stdio.h>
#include "../include/cstring.h"

int main(void) {
    cstring s = cstring_new();
    cstring_push(&_Mut s, 'h');
    cstring_push(&_Mut s, 'i');
    _Unsafe { printf("smoke len=%zu char0=%c\n", cstring_len(&_Const s), cstring_at(&_Const s, 0)); }
    cstring_free(s);
    return 0;
}

#include "../src/protocol/tonie_protocol.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    TonieTag tag = {0};
    for(size_t i = 0; i < TONIE_MAX_BLOCKS; ++i) tag.security[i] = 1;
    tag.accept_all_passwords = false;

    tonie_sanitize_dump(&tag);

    for(size_t i = 0; i < TONIE_MAX_BLOCKS; ++i) assert(tag.security[i] == 0);
    assert(tag.accept_all_passwords);

    puts("Dump sanitize: lock bits cleared, AcceptAllPasswords set PASS");
    return 0;
}

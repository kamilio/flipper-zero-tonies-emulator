#include "../src/native/quiet_recovery.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    TonieQuietRecovery r={0};
    const uint8_t inventory[]={0x26,1,0}, masked[]={0x26,1,4,9}, slots[]={6,1,0};
    const uint8_t quiet[]={0x22,2}, reset[]={2,0x26};
    /* Captured control flow with synthetic bytes: validated password; reset absent. */
    const uint8_t auth[]={2,0xb3,4,4,0xaa,0xbb,0xcc,0xdd};
    assert(!tonie_quiet_recover(&r,inventory,3,true));
    tonie_quiet_observe(&r,auth,8,true,false);
    assert(!tonie_quiet_recover(&r,inventory,3,true));
    tonie_quiet_observe(&r,auth,8,true,true);
    assert(!tonie_quiet_recover(&r,NULL,0,true));
    assert(!tonie_quiet_recover(&r,masked,4,true));
    assert(!tonie_quiet_recover(&r,slots,3,true));
    assert(!tonie_quiet_recover(&r,inventory,3,false));
    assert(tonie_quiet_recover(&r,inventory,3,true));
    assert(!tonie_quiet_recover(&r,inventory,3,true));
    tonie_quiet_observe(&r,auth,8,true,true);
    tonie_quiet_observe(&r,quiet,2,true,true);
    assert(!tonie_quiet_recover(&r,inventory,3,true));
    tonie_quiet_observe(&r,auth,8,true,true);
    tonie_quiet_observe(&r,reset,2,false,true);
    assert(!tonie_quiet_recover(&r,inventory,3,true));
    for(unsigned i=0;i<10000;++i) {
        tonie_quiet_observe(&r,quiet,2,true,true);
        tonie_quiet_observe(&r,auth,8,true,true);
        assert(tonie_quiet_recover(&r,inventory,3,true));
    }
    puts("Quiet recovery: synthetic missing-reset sequence and guard cases PASS");
}

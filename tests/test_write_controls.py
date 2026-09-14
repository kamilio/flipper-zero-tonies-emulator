#!/usr/bin/env python3
"""Exercise production input/CLI callbacks with a full simulated event queue."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/app/tonie_app.c').read_text()
def function(name):
    start = source.index('static void ' + name + '(')
    return source[start:source.index('\n}', start)+2]
state_start = source.index('typedef enum { WriterUnavailable')
state_end = source.index(';', state_start)+1
preamble = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#define UNUSED(x) ((void)(x))
typedef int PipeSide;
typedef int FuriString;
typedef enum { InputTypePress, InputTypeRelease, InputTypeShort, InputTypeLong, InputTypeRepeat } InputType;
typedef enum { InputKeyBack, InputKeyLeft, InputKeyUp, InputKeyOk, InputKeyRight, InputKeyDown } InputKey;
typedef struct { InputType type; InputKey key; } InputEvent;
typedef struct { atomic_int writer_state; atomic_bool cancel_requested; void* input_queue; } TonieApp;
static unsigned queued;
static int furi_message_queue_put(void* queue, const InputEvent* e, unsigned timeout) {
    (void)queue; (void)e; (void)timeout; ++queued; return -1; /* Always full. */
}
'''
main = r'''
int main(void) {
    TonieApp app; atomic_init(&app.writer_state,WriterUnavailable); atomic_init(&app.cancel_requested,false);
    for(int state=WriterUnavailable;state<=WriterResult;++state) {
        atomic_store(&app.writer_state,state);
        for(unsigned i=0;i<10000;++i) cli_write_chip_command(NULL,NULL,&app);
        assert(atomic_load(&app.writer_state)==(state==WriterIdle?WriterRequested:state));
    }
    atomic_store(&app.writer_state,WriterIdle);
    InputEvent down={.type=InputTypeShort,.key=InputKeyDown};
    unsigned before_down=queued; input(&down,&app); assert(queued==before_down+1);
    for(int type=InputTypePress;type<=InputTypeRepeat;++type) {
        InputEvent e={.type=type,.key=InputKeyUp}; unsigned before=queued;
        input(&e,&app); assert(queued==before+(type==InputTypeShort));
    }
    for(int state=WriterRequested;state<=WriterRunning;++state) {
        atomic_store(&app.writer_state,state);
        for(int key=InputKeyBack;key<=InputKeyDown;++key) {
            atomic_store(&app.cancel_requested,false); unsigned before=queued;
            InputEvent e={.type=InputTypeShort,.key=key}; input(&e,&app);
            assert(queued==before);
            assert(atomic_load(&app.cancel_requested)==(key==InputKeyBack || key==InputKeyLeft));
            for(unsigned i=0;i<10000;++i) cli_write_chip_command(NULL,NULL,&app);
            assert(atomic_load(&app.writer_state)==state);
            assert(atomic_load(&app.cancel_requested)==(key==InputKeyBack || key==InputKeyLeft));
        }
    }
    puts("Controls: duplicate CLI requests, busy/unavailable states, event filtering and cancellation with a full queue PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='tonie-controls-') as directory:
    c=Path(directory)/'controls.c'; binary=Path(directory)/'controls'
    c.write_text(preamble+source[state_start:state_end]+function('input')+function('cli_write_chip_command')+main)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

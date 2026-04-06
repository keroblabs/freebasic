/*
 * callframe.c — Call frame stack management
 */
#include "callframe.h"
#include <string.h>

void callstack_init(CallStack* cs) {
    cs->sp = 0;
}

int callstack_push(CallStack* cs, CallFrame* frame) {
    if (cs->sp >= CALL_STACK_MAX) return -1;
    cs->frames[cs->sp] = *frame;
    cs->sp++;
    return 0;
}

CallFrame* callstack_top(CallStack* cs) {
    if (cs->sp == 0) return NULL;
    return &cs->frames[cs->sp - 1];
}

int callstack_pop(CallStack* cs) {
    if (cs->sp == 0) return -1;
    cs->sp--;
    return 0;
}

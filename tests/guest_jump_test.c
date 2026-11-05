// Synthetic nonlocal return cases; no game content.
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "guest_jump.h"

void rt_fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    abort();
}

static void nested(Cpu *c, uint64_t target, int value) {
    RtGuestJumpScope scope __attribute__((cleanup(rt_guest_jump_cleanup))) = {c};
    c->x[0] = 0x2220;
    RtGuestJump *j = rt_guest_jump_prepare(&scope);
    if (setjmp(j->host) == 0) {
        c->x[0] = target;
        c->x[1] = (uint32_t)value;
        c->x[19] = 99;
        c->sp = 0xdead;
        rt_guest_longjmp(c);
    }
    abort(); // the outer continuation, not this one, must receive the jump
}

static void run_case(Cpu *c, int value) {
    RtGuestJumpScope scope __attribute__((cleanup(rt_guest_jump_cleanup))) = {c};
    c->x[0] = 0x1110;
    c->x[19] = 42;
    c->sp = 0x8000;
    c->v[8] = 0x1234;
    RtGuestJump *j = rt_guest_jump_prepare(&scope);
    if (setjmp(j->host) == 0) nested(c, 0x1110, value);
    else rt_guest_jump_landed(c);
    assert((int32_t)c->x[0] == (value ? value : 1));
    assert(c->x[19] == 42 && c->sp == 0x8000 && c->v[8] == 0x1234);
}

int main(void) {
    Cpu *c = calloc(1, sizeof *c);
    assert(c);
    for (int i = 0; i < 100; i++) {
        run_case(c, 0);
        run_case(c, 7);
        run_case(c, -3);
    }
    free(c);
    puts("Guest setjmp/longjmp: nested unwinding, register restore and return values passed");
    return 0;
}

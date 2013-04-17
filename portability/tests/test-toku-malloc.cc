/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2013 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."
#include <stdio.h>
#include <toku_assert.h>
#include <memory.h>
#include <toku_pthread.h>

static void *f(void *arg) {
    void *vp = toku_malloc(32);
    assert(vp);
    toku_free(vp);
    return arg;
}

int main(void) {
    int r;
    int i;
    const int max_threads = 2;
    toku_pthread_t tids[max_threads];
    for (i=0; i<max_threads; i++) {
        r = toku_pthread_create(&tids[i], NULL, f, 0); assert(r == 0);
    }
    for (i=0; i<max_threads; i++) {
        void *ret;
        r = toku_pthread_join(tids[i], &ret); assert(r == 0);
    }
    return 0;
}

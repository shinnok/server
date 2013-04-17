/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2013 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#ifndef UTIL_KIBBUTZ_H
#define UTIL_KIBBUTZ_H

//
// The kibbutz is another threadpool meant to do arbitrary work.
//

typedef struct kibbutz *KIBBUTZ;
//
// create a kibbutz where n_workers is the number of threads in the threadpool
//
KIBBUTZ toku_kibbutz_create (int n_workers);
//
// enqueue a workitem in the kibbutz. When the kibbutz is to work on this workitem,
// it calls f(extra). 
// At any time, the kibbutz is operating on at most n_workers jobs. 
// Other enqueued workitems are on a queue. An invariant is 
// that no currently enqueued item was placed on the queue before 
// any item that is currently being operated on. Another way to state
// this is that all items on the queue were placed there before any item
// that is currently being worked on
//
void toku_kibbutz_enq (KIBBUTZ k, void (*f)(void*), void *extra);
//
// destroys the kibbutz
//
void toku_kibbutz_destroy (KIBBUTZ k);

#endif // UTIL_KIBBUTZ_H

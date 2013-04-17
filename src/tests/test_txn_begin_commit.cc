/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#ident "$Id$"
#ident "Copyright (c) 2007-2013 Tokutek Inc.  All rights reserved."
#include "test.h"
#include <stdio.h>

#include <sys/stat.h>
#include <db.h>


int
test_main(int UU(argc), char UU(*const argv[])) {
    int r;
    DB_ENV *env;

    toku_os_recursive_delete(TOKU_TEST_FILENAME);
    toku_os_mkdir(TOKU_TEST_FILENAME, S_IRWXU+S_IRWXG+S_IRWXO);

    r = db_env_create(&env, 0); 
    assert(r == 0);

    r = env->open(env, TOKU_TEST_FILENAME, DB_INIT_MPOOL + DB_INIT_LOG + DB_INIT_TXN + DB_PRIVATE + DB_CREATE, S_IRWXU+S_IRWXG+S_IRWXO); 
    assert(r == 0);

    DB_TXN *txn;
    r = env->txn_begin(env, 0, &txn, 0);
    assert(r == 0);

    r = txn->commit(txn, 0); 
    assert(r == 0);
    
r = env->close(env, 0); 
    assert(r == 0);
    return 0;
}

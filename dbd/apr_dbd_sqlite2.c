/* Copyright 2000-2005 The Apache Software Foundation or its licensors, as
 * applicable.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apu.h"

#if APU_HAVE_SQLITE2

#include <ctype.h>
#include <stdlib.h>

#include <sqlite.h>

#include "apr_strings.h"
#include "apr_time.h"

#include "apr_dbd_internal.h"

struct apr_dbd_transaction_t {
    int errnum;
    apr_dbd_t *handle;
};

struct apr_dbd_t {
    sqlite *conn;
    char *errmsg;
    apr_dbd_transaction_t *trans;
};

struct apr_dbd_results_t {
    int random;
    sqlite *handle;
    char **res;
    size_t ntuples;
    size_t sz;
    size_t index;
};

struct apr_dbd_row_t {
    int n;
    char **data;
    apr_dbd_results_t *res;
};

struct apr_dbd_prepared_t {
    const char *name;
    int prepared;
};

#define FREE_ERROR_MSG(dbd) \
	do { \
		if(dbd && dbd->errmsg) { \
			free(dbd->errmsg); \
			dbd->errmsg = NULL; \
		} \
	} while(0);

static apr_status_t free_table(void *data)
{
    sqlite_free_table(data); 
    return APR_SUCCESS;
}

static int dbd_sqlite_select(apr_pool_t * pool, apr_dbd_t * sql,
                             apr_dbd_results_t ** results, const char *query,
                             int seek)
{
    char **result;
    int ret = 0;
    int tuples = 0;
    int fields = 0;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    FREE_ERROR_MSG(sql);

    ret = sqlite_get_table(sql->conn, query, &result, &tuples, &fields,
                          &sql->errmsg);

    if (ret == SQLITE_OK) {
        if (!*results) {
            *results = apr_pcalloc(pool, sizeof(apr_dbd_results_t));
        }

        (*results)->res = result;
        (*results)->ntuples = tuples;
        (*results)->sz = fields;
        (*results)->random = seek;

        if (tuples > 0)
            apr_pool_cleanup_register(pool, result, free_table,
                                      apr_pool_cleanup_null);

        ret = 0;
    }
    else {
        sql->trans->errnum = ret;
    }

    return ret;
}

static const char *dbd_sqlite_get_name(const apr_dbd_results_t *res, int n)
{
    if ((n < 0) || (n >= res->sz)) {
        return NULL;
    }

    return res->res[n];
}

static int dbd_sqlite_get_row(apr_pool_t * pool, apr_dbd_results_t * res,
                              apr_dbd_row_t ** rowp, int rownum)
{
    apr_dbd_row_t *row = *rowp;
    int sequential = ((rownum >= 0) && res->random) ? 0 : 1;

    if (row == NULL) {
        row = apr_palloc(pool, sizeof(apr_dbd_row_t));
        *rowp = row;
        row->res = res;
        row->n = sequential ? 0 : rownum - 1;
    }
    else {
        if (sequential) {
            ++row->n;
        }
        else {
            row->n = rownum - 1;
        }
    }

    if (row->n >= res->ntuples) {
        *rowp = NULL;
        apr_pool_cleanup_run(pool, res->res, free_table);
        res->res = NULL;
        return -1;
    }

    /* Pointer magic explanation:
     *      The sqlite result is an array such that the first res->sz elements are 
     *      the column names and each tuple follows afterwards 
     *      ex: (from the sqlite2 documentation)
     SELECT employee_name, login, host FROM users WHERE login LIKE *        'd%';

     nrow = 2
     ncolumn = 3
     result[0] = "employee_name"
     result[1] = "login"
     result[2] = "host"
     result[3] = "dummy"
     result[4] = "No such user"
     result[5] = 0
     result[6] = "D. Richard Hipp"
     result[7] = "drh"
     result[8] = "zadok"
     */

    row->data = res->res + res->sz + (res->sz * row->n);

    return 0;
}

static const char *dbd_sqlite_get_entry(const apr_dbd_row_t * row, int n)
{
    if ((n < 0) || (n >= row->res->sz)) {
      return NULL;
    }

    return row->data[n];
}

static const char *dbd_sqlite_error(apr_dbd_t * sql, int n)
{
    return sql->errmsg;
}

static int dbd_sqlite_query(apr_dbd_t * sql, int *nrows, const char *query)
{
    char **result;
    int ret;
    int tuples = 0;
    int fields = 0;

    if (sql->trans && sql->trans->errnum) {
        return sql->trans->errnum;
    }

    FREE_ERROR_MSG(sql);

    ret =
        sqlite_get_table(sql->conn, query, &result, &tuples, &fields,
                         &sql->errmsg);
    if (ret == SQLITE_OK) {
        *nrows = sqlite_changes(sql->conn);

        if (tuples > 0)
            free(result);

        ret = 0;
    }

    if (sql->trans) {
        sql->trans->errnum = ret;
    }

    return ret;
}

static apr_status_t free_mem(void *data)
{
    sqlite_freemem(data);
    return APR_SUCCESS;
}

static const char *dbd_sqlite_escape(apr_pool_t * pool, const char *arg,
                                     apr_dbd_t * sql)
{
    char *ret = sqlite_mprintf("%q", arg);
    apr_pool_cleanup_register(pool, ret, free_mem, apr_pool_cleanup_null);
    return ret;
}

static int dbd_sqlite_prepare(apr_pool_t * pool, apr_dbd_t * sql,
                              const char *query, const char *label,
                              apr_dbd_prepared_t ** statement)
{
    return APR_ENOTIMPL;
}

static int dbd_sqlite_pquery(apr_pool_t * pool, apr_dbd_t * sql,
                             int *nrows, apr_dbd_prepared_t * statement,
                             int nargs, const char **values)
{
    return APR_ENOTIMPL;
}

static int dbd_sqlite_pvquery(apr_pool_t * pool, apr_dbd_t * sql,
                              int *nrows, apr_dbd_prepared_t * statement,
                              va_list args)
{
    return APR_ENOTIMPL;
}

static int dbd_sqlite_pselect(apr_pool_t * pool, apr_dbd_t * sql,
                              apr_dbd_results_t ** results,
                              apr_dbd_prepared_t * statement,
                              int seek, int nargs, const char **values)
{
    return APR_ENOTIMPL;
}

static int dbd_sqlite_pvselect(apr_pool_t * pool, apr_dbd_t * sql,
                               apr_dbd_results_t ** results,
                               apr_dbd_prepared_t * statement, int seek,
                               va_list args)
{
    return APR_ENOTIMPL;
}

static int dbd_sqlite_start_transaction(apr_pool_t * pool, apr_dbd_t * handle,
                                        apr_dbd_transaction_t ** trans)
{
    int ret, rows;

    ret = dbd_sqlite_query(handle, &rows, "BEGIN TRANSACTION");
    if (ret == 0) {
        if (!*trans) {
            *trans = apr_pcalloc(pool, sizeof(apr_dbd_transaction_t));
        }
        (*trans)->handle = handle;
        handle->trans = *trans;
    }
    else {
        ret = -1;
    }
    return ret;
}

static int dbd_sqlite_end_transaction(apr_dbd_transaction_t * trans)
{
    int rows;
    int ret = -1;               /* no transaction is an error cond */

    if (trans) {
        if (trans->errnum) {
            trans->errnum = 0;
            ret =
                dbd_sqlite_query(trans->handle, &rows,
                                 "ROLLBACK TRANSACTION");
        }
        else {
            ret =
                dbd_sqlite_query(trans->handle, &rows, "COMMIT TRANSACTION");
        }
        trans->handle->trans = NULL;
    }

    return ret;
}

static apr_dbd_t *dbd_sqlite_open(apr_pool_t * pool, const char *params_)
{
    apr_dbd_t *sql;
    sqlite *conn = NULL;
    char *perm;
    int iperms = 600;
    char* params = apr_pstrdup(pool, params_);
    /* params = "[filename]:[permissions]"
     *    example: "shopping.db:600"
     */

    perm = strstr(params, ":");
    if (perm) {
        *(perm++) = '\x00';     /* split the filename and permissions */

        if (strlen(perm) > 0)
            iperms = atoi(perm);
    }

    conn = sqlite_open(params, iperms, NULL);

    sql = apr_pcalloc(pool, sizeof(*sql));
    sql->conn = conn;

    return sql;
}

static apr_status_t dbd_sqlite_close(apr_dbd_t * handle)
{
    if (handle->conn) {
        sqlite_close(handle->conn);
        handle->conn = NULL;
    }
    return APR_SUCCESS;
}

static apr_status_t dbd_sqlite_check_conn(apr_pool_t * pool,
                                          apr_dbd_t * handle)
{
    if (handle->conn == NULL)
        return -1;
    return APR_SUCCESS;
}

static int dbd_sqlite_select_db(apr_pool_t * pool, apr_dbd_t * handle,
                                const char *name)
{
    return APR_ENOTIMPL;
}

static void *dbd_sqlite_native(apr_dbd_t * handle)
{
    return handle->conn;
}

static int dbd_sqlite_num_cols(apr_dbd_results_t * res)
{
    return res->sz;
}

static int dbd_sqlite_num_tuples(apr_dbd_results_t * res)
{
    return res->ntuples;
}

APU_DECLARE_DATA const apr_dbd_driver_t apr_dbd_sqlite2_driver = {
    "sqlite2",
    NULL,
    dbd_sqlite_native,
    dbd_sqlite_open,
    dbd_sqlite_check_conn,
    dbd_sqlite_close,
    dbd_sqlite_select_db,
    dbd_sqlite_start_transaction,
    dbd_sqlite_end_transaction,
    dbd_sqlite_query,
    dbd_sqlite_select,
    dbd_sqlite_num_cols,
    dbd_sqlite_num_tuples,
    dbd_sqlite_get_row,
    dbd_sqlite_get_entry,
    dbd_sqlite_error,
    dbd_sqlite_escape,
    dbd_sqlite_prepare,
    dbd_sqlite_pvquery,
    dbd_sqlite_pvselect,
    dbd_sqlite_pquery,
    dbd_sqlite_pselect,
    dbd_sqlite_get_name
};
#endif

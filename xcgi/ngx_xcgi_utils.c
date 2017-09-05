/*
 * Copyright (C) lurenfu@qq.com
 */

#include "ngx_xcgi_private.h"

static ngx_xcgi_handler_t
                g_xcgi_handler_hash_table[NGX_XCGI_HANDLER_HASH_TABLE_SIZE];

static ngx_uint_t ngx_xcgi_hash(ngx_str_t *str)
{
    ngx_uint_t hash = 0;

    for (size_t i=0; i < str->len; i++) {
        if (!(i&1)) {
            hash ^= ((hash << 7) ^ (str->data[i]) ^ (hash >> 3));
        } else {
            hash ^= (~((hash << 11) ^ (str->data[i]) ^ (hash >> 5)));
        }
    }

    return (hash & (NGX_XCGI_HANDLER_HASH_TABLE_SIZE-1));
}

static ngx_uint_t ngx_xcgi_hash_str(char *str)
{
    ngx_str_t   ngxstr;

    ngxstr.data = (u_char *)str;
    ngxstr.len = strlen(str);
    return ngx_xcgi_hash(&ngxstr);
}

void ngx_xcgi_register_handlers(ngx_xcgi_handler_t *h, int n)
{
    int             i;
    ngx_uint_t      key;

    for (i = 0; i < n; i++) {
        key = ngx_xcgi_hash(&h[i].name);
        h[i].next = &g_xcgi_handler_hash_table[key];
        h[i].prev = g_xcgi_handler_hash_table[key].prev;
        g_xcgi_handler_hash_table[key].prev->next = h+i;
        g_xcgi_handler_hash_table[key].prev = h+i;
    }
}

static void ngx_xcgi_unescape_value(ngx_http_request_t *r, char *value)
{
    int     i, j, len;
    char    *str, c1, c2, ch;

    if ((!strchr(value, '%') )&&( !strchr(value, '+'))) {
        return;
    }

    len = (int)strlen(value);
    str = ngx_palloc(r->pool, len+1);
    memcpy(str, value, len);
    str[len] = 0;

    i = j = 0;
    while (str[i] != 0) {
        if (str[i] != '%') {
            if(str[i] == '+') {
                value[j++] = ' ';
                i++;
            } else {
                value[j++] = str[i++];
            }
        } else {
            c1 = str[i+1];
            c2 = str[i+2];
            if (c1 >= '0' && c1 <= '9') {
                c1 -= '0';
            } else if (c1 >= 'a' && c1 <= 'f') {
                c1 = c1 - 'a' + 10;
            } else if (c1 >= 'A' && c1 <= 'F') {
                c1 = c1 - 'A' + 10;
            }

            if (c2 >= '0' && c2 <= '9') {
                c2 -= '0';
            } else if (c2 >= 'a' && c2 <= 'f') {
                c2 = c2 - 'a' + 10;
            } else if (c2 >= 'A' && c2 <= 'F') {
                c2 = c2 - 'A' + 10;
            }

            ch = ((c1&0xf)<<4)|(c2&0xf);
            value[j++] = ch;
            i += 3;
        }
    }
    value[j] = 0;
    ngx_pfree(r->pool, str);
}

int ngx_xcgi_set_var_internal(ngx_http_request_t *r, ngx_xcgi_handler_t *var)
{
    ngx_xcgi_handler_t *p = (ngx_xcgi_handler_t *)r->xcgi_var;
    ngx_uint_t          key = ngx_xcgi_hash(&var->name);;

    if (!p) {
        return -1;
    }

    ngx_xcgi_unescape_value(r, var->data.value);

    var->prev = &p[key];
    var->next = p[key].next;
    p[key].next->prev = var;
    p[key].next = var;

    return 0;
}

int ngx_xcgi_set_var(ngx_http_request_t *r, const char *name, const char *value)
{
    ngx_xcgi_handler_t  *var;

    if (!r || !name || !value) {
        return -1;
    }

    var = ngx_pcalloc(r->pool, sizeof(ngx_xcgi_handler_t));
    if (!var) {
        return -1;
    }

    var->name.len = strlen(name);
    var->name.data = ngx_pcalloc(r->pool, var->name.len);
    if (!var->name.data) {
        return -1;
    }
    memcpy(var->name.data, name, var->name.len);

    var->data.value = ngx_pcalloc(r->pool, strlen(value)+1);
    if (!var->data.value) {
        return -1;
    }
    strcpy(var->data.value, value);

    return ngx_xcgi_set_var_internal(r, var);
}

char *ngx_xcgi_get_var(ngx_http_request_t *r, char *name, char *default_value)
{
    if (r->xcgi_var)
    {
        ngx_uint_t      key = ngx_xcgi_hash_str(name);
        ngx_xcgi_handler_t   *h = ((ngx_xcgi_handler_t *)r->xcgi_var) + key;
        ngx_xcgi_handler_t   *p = h->next;

        while(p != h) {
            if (p->name.len == strlen(name) &&
                !ngx_strncmp(p->name.data, name, p->name.len))
            {
                return p->data.value;
            }
            p = p->next;
        }
    }

    return default_value;
}

ngx_int_t ngx_xcgi_call_handler(ngx_str_t *name, ngx_http_request_t *r,
                                ngx_buf_t *b, int argc, char **argv)
{
    ngx_uint_t      key = ngx_xcgi_hash(name);
    ngx_xcgi_handler_t   *p = g_xcgi_handler_hash_table[key].next;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "ngx_xcgi_call_handler key=%d, name=\"%V\"", key, name);

    while (p != &g_xcgi_handler_hash_table[key]) {
        if (p->name.len == name->len &&
            !ngx_strncmp(p->name.data, name->data, name->len))
        {
            if (p->data.func) {
                return (*p->data.func)(r, b, argc, argv);
            }
            return NGX_OK;
        }
        p = p->next;
    }

    return NGX_ERROR;
}

int ngx_xcgi_write(ngx_http_request_t *r, ngx_buf_t *b, char *fmt, ...)
{
    va_list     args;
    char        str[4096];
    int         slen, bleft, blen, bsize;

    va_start(args, fmt);
    slen = vsnprintf(str, sizeof(str)-1, fmt, args);
    va_end(args);
    blen = b->last - b->start;
    bleft = b->end - b->last;
    bsize = b->end - b->start;
    if (slen > bleft) {
        void    *nbuf = ngx_palloc(r->pool, bsize<<1);
        if (nbuf) {
            ngx_memcpy(nbuf, b->start, blen);
            ngx_pfree(r->pool, b->start);
            b->start = nbuf;
            b->pos = b->start;
            b->last = b->start + blen;
            b->end = b->start + (bsize<<1);
            bleft += bsize;
        }
    }

    if (slen > bleft) {
        slen = bleft;
    }

    b->last = ngx_cpymem(b->last, str, slen);
    return slen;
}

void ngx_xcgi_init_xcgi_variable(ngx_http_request_t *r)
{
    if (NULL == r->xcgi_var) {
        r->xcgi_var = ngx_pcalloc(r->pool,
                NGX_XCGI_HANDLER_HASH_TABLE_SIZE*sizeof(ngx_xcgi_handler_t));

        if (r->xcgi_var) {
            int              i;
            ngx_xcgi_handler_t  *var = (ngx_xcgi_handler_t *)r->xcgi_var;

            for (i = 0; i < NGX_XCGI_HANDLER_HASH_TABLE_SIZE; i++) {
                var[i].prev = var[i].next = &var[i];
            }
        }
    }

}

void ngx_xcgi_private_init(void)
{
    for (int i = 0; i < NGX_XCGI_HANDLER_HASH_TABLE_SIZE; i++) {
        g_xcgi_handler_hash_table[i].data.func = NULL;
        g_xcgi_handler_hash_table[i].name.data = NULL;
        g_xcgi_handler_hash_table[i].name.len = 0;
        g_xcgi_handler_hash_table[i].next = &g_xcgi_handler_hash_table[i];
        g_xcgi_handler_hash_table[i].prev = &g_xcgi_handler_hash_table[i];
    }

    ngx_xcgi_register_user_handlers();
}


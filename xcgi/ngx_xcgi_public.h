/*
 * Copyright (C) lurenfu@qq.com
 */


#ifndef __NGX_XCGI_PUBLIC_H_INCLUDED__
#define __NGX_XCGI_PUBLIC_H_INCLUDED__


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef int (*ngx_xcgi_pfunc)(ngx_http_request_t *r, ngx_buf_t *b,
                              int argc, char **argv);

typedef struct ngx_xcgi_handler_s ngx_xcgi_handler_t;

struct ngx_xcgi_handler_s {
    ngx_xcgi_handler_t  *next;
    ngx_xcgi_handler_t  *prev;
    ngx_str_t            name;
    union {
        ngx_xcgi_pfunc   func;
        char            *value;
    } data;
};


/*
 * The following functions could be called by user handlers.
 */

char* ngx_xcgi_get_var(ngx_http_request_t *r, char *name, char *default_value);

int   ngx_xcgi_set_var(ngx_http_request_t *r, const char *name,
                       const char *value);

int   ngx_xcgi_write(ngx_http_request_t *r, ngx_buf_t *b, char *fmt, ...);

void  ngx_xcgi_register_handlers(ngx_xcgi_handler_t *handlers, int n);

/*
 * The end user should implement ngx_xcgi_register_user_handlers to register
 * all handlers for your web application based on nginx_http_xcgi_module.
 * Please reference ngx_xcgi_example_handlers.c
 */

void  ngx_xcgi_register_user_handlers(void);


#endif /* __NGX_XCGI_PUBLIC_H_INCLUDED__ */

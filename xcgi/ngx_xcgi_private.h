/*
 * Copyright (C) lurenfu@qq.com
 */


#ifndef __NGX_XCGI_PRIVATE_H_INCLUDED__
#define __NGX_XCGI_PRIVATE_H_INCLUDED__


#include "ngx_xcgi_public.h"

#define NGX_XCGI_HANDLER_HASH_TABLE_SIZE    256

void  ngx_xcgi_private_init(void);

void  ngx_xcgi_register_user_handlers(void);

void  ngx_xcgi_init_xcgi_variable(ngx_http_request_t *r);

int   ngx_xcgi_set_var_internal(ngx_http_request_t *r, ngx_xcgi_handler_t *var);

ngx_int_t   ngx_xcgi_call_handler(ngx_str_t *name, ngx_http_request_t *r,
                            ngx_buf_t *b, int argc, char **argv);


#endif /* __NGX_XCGI_PRIVATE_H_INCLUDED__ */

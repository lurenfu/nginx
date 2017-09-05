/*
 * Copyright (C) lurenfu@qq.com
 */

#include "ngx_xcgi_public.h"

static char *str_xcgi_copyleft =
"XCGI_CopyLeft = {\n"
"\tauthor: \"lurenfu@qq.com\",\n"
"\tbirth:  \"2013-07-01\"\n"
"};\n";

static int XCGI_CopyLeft(ngx_http_request_t *r, ngx_buf_t *b,
                         int argc, char **argv)
{
    return ngx_xcgi_write(r, b, "%s", str_xcgi_copyleft);
}

static int XCGI_HelloWorld(ngx_http_request_t *r, ngx_buf_t *b,
                            int argc, char **argv)
{
    return ngx_xcgi_write(r, b, "XCGI: Hello, world!");
}

static int XCGI_AJAX_Add(ngx_http_request_t *r, ngx_buf_t *b,
                            int argc, char **argv)
{
    int n1, n2, n = 0;

    n1 = atoi(ngx_xcgi_get_var(r, "num1", ""));
    n2 = atoi(ngx_xcgi_get_var(r, "num2", ""));
    n = n1 + n2;

    return ngx_xcgi_write(r, b, "%d", n);
}

static int XCGI_StringJoin(ngx_http_request_t *r, ngx_buf_t *b,
                            int argc, char **argv)
{
    int i, n = 0;

    for (i = 0; i < argc; i++) {
        n += ngx_xcgi_write(r, b, "%s%s", argv[i], (i < (argc-1))?"--":"");
    }

    return n;
}

static ngx_xcgi_handler_t   xcgi_example_handlers[] = {
    {
        .name = ngx_string("XCGI_CopyLeft"),
        .data.func = XCGI_CopyLeft,
    },
    {
        .name = ngx_string("XCGI_HelloWorld"),
        .data.func = XCGI_HelloWorld,
    },
    {
        .name = ngx_string("XCGI_AJAX_Add"),
        .data.func = XCGI_AJAX_Add,
    },
    {
        .name = ngx_string("XCGI_StringJoin"),
        .data.func = XCGI_StringJoin,
    },
};

void ngx_xcgi_register_user_handlers(void)
{
    int size = sizeof(xcgi_example_handlers)/sizeof(xcgi_example_handlers[0]);

    ngx_xcgi_register_handlers(xcgi_example_handlers, size);
}

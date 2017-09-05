
/*
 * code original from ngx_http_sub_filter_module.c
 * Copyright (C) lurenfu@qq.com
 */


#include "ngx_xcgi_private.h"

#define NGX_HTTP_XCGI_COMMAND_LEN       64
#define NGX_HTTP_XCGI_PARAM_COUNT       8
#define NGX_HTTP_XCGI_PARAM_LEN         1023
#define NGX_HTTP_XCGI_PARAM_TOTAL_LEN   \
            (NGX_HTTP_XCGI_PARAM_COUNT*(NGX_HTTP_XCGI_PARAM_LEN+1))

static ngx_str_t s_xcgi_form_prefix = ngx_string("/XCGI_Form/");

typedef enum {
    xcgi_init_state = 0,
    xcgi_start_state,
    xcgi_precommand_state,
    xcgi_command_state,
    xcgi_postcommand_state,
    xcgi_preparam_state,
    xcgi_postparam_state,
    xcgi_intparam_state,
    xcgi_postintparam_state,
    xcgi_stringparam_state,
    xcgi_poststringparam_state,
    xcgi_stringescape_state,
    xcgi_nextparam_state,
    xcgi_error_state,
    xcgi_end_state,
} ngx_http_xcgi_state_e;


typedef struct {

    ngx_buf_t              *buf;

    u_char                 *pos;
    u_char                 *copy_start;
    u_char                 *copy_end;

    ngx_chain_t            *in;
    ngx_chain_t            *out;
    ngx_chain_t           **last_out;
    ngx_chain_t            *busy;
    ngx_chain_t            *free;

    ngx_uint_t              state;
    ngx_str_t               command;
    int                     argc;
    char                   *argv[NGX_HTTP_XCGI_PARAM_COUNT];
    int                     argvlen[NGX_HTTP_XCGI_PARAM_COUNT];
    char                    argvdata[NGX_HTTP_XCGI_PARAM_TOTAL_LEN];
} ngx_http_xcgi_ctx_t;


static ngx_int_t ngx_http_xcgi_output(ngx_http_request_t  *r,
                                      ngx_http_xcgi_ctx_t *ctx);

static ngx_int_t ngx_http_xcgi_parse(ngx_http_request_t   *r,
                                     ngx_http_xcgi_ctx_t  *ctx);

static ngx_int_t ngx_http_xcgi_module_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_xcgi_filter_init(ngx_conf_t *cf);
static void      ngx_http_xcgi_ctx_init(ngx_http_request_t  *r,
                                     ngx_http_xcgi_ctx_t *ctx);


static ngx_http_module_t ngx_http_xcgi_filter_module_ctx = {
    ngx_http_xcgi_module_init,              /* preconfiguration */
    ngx_http_xcgi_filter_init,              /* postconfiguration */

    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */

    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */

    NULL,                                   /* create location configuration */
    NULL                                    /* merge location configuration */
};


ngx_module_t ngx_http_xcgi_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_xcgi_filter_module_ctx,       /* module context */
    NULL,                                   /* module directives */
    NGX_HTTP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;

static ngx_int_t
ngx_http_xcgi_header_filter(ngx_http_request_t *r)
{
    ngx_http_xcgi_ctx_t        *ctx;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http xcgi header filter \"%V\"", &r->uri);

    if (r->headers_out.content_length_n == 0)
    {
        return ngx_http_next_header_filter(r);
    }

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_xcgi_ctx_t));
    if (ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_http_set_ctx(r, ctx, ngx_http_xcgi_filter_module);

    ctx->last_out = &ctx->out;

    r->filter_need_in_memory = 1;

    if (r == r->main) {
        ngx_http_clear_content_length(r);
        ngx_http_clear_last_modified(r);
    }

    return ngx_http_next_header_filter(r);
}


static ngx_int_t
ngx_http_xcgi_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_int_t                  rc;
    ngx_buf_t                 *b;
    ngx_chain_t               *cl;
    ngx_http_xcgi_ctx_t        *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_xcgi_filter_module);
    if (ctx == NULL) {
        return ngx_http_next_body_filter(r, in);
    }

    /* only scan *.htm or *.html files for xcgi_modules */
    {
        int len = r->uri.len;

        if (len < 5 || len > 256) {
            return ngx_http_next_body_filter(r, in);
        }

        if (memcmp(r->uri.data + r->uri.len - 4, ".htm", 4) &&
            memcmp(r->uri.data + r->uri.len - 5, ".html", 5))
        {
            return ngx_http_next_body_filter(r, in);
        }
    }

    if ((in == NULL
         && ctx->buf == NULL
         && ctx->in == NULL
         && ctx->busy == NULL))
    {
        return ngx_http_next_body_filter(r, in);
    }


    /* add the incoming chain to the chain ctx->in */

    if (in) {
        if (ngx_chain_add_copy(r->pool, &ctx->in, in) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http xcgi filter \"%V\"", &r->uri);

    while (ctx->in || ctx->buf) {

        if (ctx->buf == NULL) {
            ctx->buf = ctx->in->buf;
            ctx->in = ctx->in->next;
            ctx->pos = ctx->buf->pos;
        }

        if (ctx->state == xcgi_init_state) {
            ctx->copy_start = ctx->pos;
            ctx->copy_end = ctx->pos;
        }

        b = NULL;

        while (ctx->pos < ctx->buf->last) {

            rc = ngx_http_xcgi_parse(r, ctx);
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "xcgi parse=%d", rc);

            if (rc == NGX_ERROR) {
                return rc;
            }

            if (ctx->copy_start != ctx->copy_end) {

                if (ctx->free) {
                    cl = ctx->free;
                    ctx->free = ctx->free->next;
                    b = cl->buf;

                } else {
                    b = ngx_alloc_buf(r->pool);
                    if (b == NULL) {
                        return NGX_ERROR;
                    }

                    cl = ngx_alloc_chain_link(r->pool);
                    if (cl == NULL) {
                        return NGX_ERROR;
                    }

                    cl->buf = b;
                }

                ngx_memcpy(b, ctx->buf, sizeof(ngx_buf_t));

                b->pos = ctx->copy_start;
                b->last = ctx->copy_end;
                b->shadow = NULL;
                b->last_buf = 0;
                b->recycled = 0;

                if (b->in_file) {
                    b->file_last = b->file_pos + (b->last - ctx->buf->pos);
                    b->file_pos += b->pos - ctx->buf->pos;
                }

                cl->next = NULL;
                *ctx->last_out = cl;
                ctx->last_out = &cl->next;
            }

            if (ctx->state == xcgi_init_state) {
                ctx->copy_start = ctx->pos;
                ctx->copy_end = ctx->pos;

            } else {
                ctx->copy_start = NULL;
                ctx->copy_end = NULL;
            }

            if (rc == NGX_AGAIN) {
                continue;
            }

            b = ngx_create_temp_buf(r->pool, 4096);
            if (b == NULL) {
                return NGX_ERROR;
            }

            cl = ngx_alloc_chain_link(r->pool);
            if (cl == NULL) {
                return NGX_ERROR;
            }

            ngx_xcgi_call_handler(&ctx->command, r, b, ctx->argc, ctx->argv);
            ngx_http_xcgi_ctx_init(r, ctx);
            if (b->last == b->pos) {
                ngx_pfree(r->pool, b);
                continue;
            }

            cl->buf = b;
            cl->next = NULL;
            *ctx->last_out = cl;
            ctx->last_out = &cl->next;

            continue;
        }

        if (ctx->buf->last_buf || ngx_buf_in_memory(ctx->buf)) {
            if (b == NULL) {
                if (ctx->free) {
                    cl = ctx->free;
                    ctx->free = ctx->free->next;
                    b = cl->buf;
                    ngx_memzero(b, sizeof(ngx_buf_t));

                } else {
                    b = ngx_calloc_buf(r->pool);
                    if (b == NULL) {
                        return NGX_ERROR;
                    }

                    cl = ngx_alloc_chain_link(r->pool);
                    if (cl == NULL) {
                        return NGX_ERROR;
                    }

                    cl->buf = b;
                }

                b->sync = 1;

                cl->next = NULL;
                *ctx->last_out = cl;
                ctx->last_out = &cl->next;
            }

            b->last_buf = ctx->buf->last_buf;
            b->shadow = ctx->buf;

            b->recycled = ctx->buf->recycled;
        }

        ctx->buf = NULL;
    }

    if (ctx->out == NULL && ctx->busy == NULL) {
        return NGX_OK;
    }

    return ngx_http_xcgi_output(r, ctx);
}


static ngx_int_t
ngx_http_xcgi_output(ngx_http_request_t *r, ngx_http_xcgi_ctx_t *ctx)
{
    ngx_int_t     rc;
    ngx_buf_t    *b;
    ngx_chain_t  *cl;

    rc = ngx_http_next_body_filter(r, ctx->out);

    if (ctx->busy == NULL) {
        ctx->busy = ctx->out;
    } else {
        for (cl = ctx->busy; cl->next; cl = cl->next) { /* void */ }
        cl->next = ctx->out;
    }

    ctx->out = NULL;
    ctx->last_out = &ctx->out;

    while (ctx->busy) {
        cl = ctx->busy;
        b = cl->buf;

        if (ngx_buf_size(b) != 0) {
            break;
        }

        if (b->shadow) {
            b->shadow->pos = b->shadow->last;
        }

        ctx->busy = cl->next;

        if (ngx_buf_in_memory(b) || b->in_file) {
            /* add data bufs only to the free buf chain */

            cl->next = ctx->free;
            ctx->free = cl;
        }
    }

    if (ctx->in || ctx->buf) {
        r->buffered |= NGX_HTTP_XCGI_BUFFERED;
    } else {
        r->buffered &= ~NGX_HTTP_XCGI_BUFFERED;
    }

    return rc;
}


static void
ngx_http_xcgi_ctx_init(ngx_http_request_t *r, ngx_http_xcgi_ctx_t *ctx)
{
    int     i;

    if (NULL == ctx->command.data) {
        ctx->command.data = ngx_pnalloc(r->pool, NGX_HTTP_XCGI_COMMAND_LEN);
    }
    ctx->command.len = 0;

    ctx->argc = 0;
    memset(ctx->argvdata, 0, sizeof(ctx->argvdata));

    for (i = 0; i < NGX_HTTP_XCGI_PARAM_COUNT; i++) {
        ctx->argvlen[i] = 0;
        ctx->argv[i] = ctx->argvdata + (i*(NGX_HTTP_XCGI_PARAM_LEN+1));
    }
}

static ngx_int_t
ngx_http_xcgi_parse(ngx_http_request_t *r, ngx_http_xcgi_ctx_t *ctx)
{
    u_char                *p, *last, *copy_end, ch;
    ngx_http_xcgi_state_e   state;

    state = ctx->state;
    last = ctx->buf->last;
    copy_end = ctx->copy_end;

    for (p = ctx->pos; p < last; p++) {
        ch = *p;
        if (state == xcgi_init_state) {
            for ( ;; ) {
                if (ch == '<') {
                    copy_end = p;
                    state = xcgi_start_state;
                    goto xcgi_started;
                }

                if (++p == last) {
                    break;
                }

                ch = *p;
            }

            ctx->state = state;
            ctx->pos = p;
            ctx->copy_end = p;

            if (ctx->copy_start == NULL) {
                ctx->copy_start = ctx->buf->pos;
            }

            return NGX_AGAIN;

        xcgi_started:

            continue;
        }

        switch (state) {

        case xcgi_init_state:
            break;

        case xcgi_start_state:
            switch (ch) {
            case '%':
                state = xcgi_precommand_state;
                ngx_http_xcgi_ctx_init(r, ctx);
                break;

            case '<':
                copy_end = p;
                break;

            default:
                copy_end = p;
                state = xcgi_init_state;
                break;
            }

            break;

        case xcgi_precommand_state:
            switch (ch) {
            case ' ':
            case CR:
            case LF:
            case '\t':
                break;

            case '_':
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'e':
            case 'f':
            case 'g':
            case 'h':
            case 'i':
            case 'j':
            case 'k':
            case 'l':
            case 'm':
            case 'n':
            case 'o':
            case 'p':
            case 'q':
            case 'r':
            case 's':
            case 't':
            case 'u':
            case 'v':
            case 'w':
            case 'x':
            case 'y':
            case 'z':
            case 'A':
            case 'B':
            case 'C':
            case 'D':
            case 'E':
            case 'F':
            case 'G':
            case 'H':
            case 'I':
            case 'J':
            case 'K':
            case 'L':
            case 'M':
            case 'N':
            case 'O':
            case 'P':
            case 'Q':
            case 'R':
            case 'S':
            case 'T':
            case 'U':
            case 'V':
            case 'W':
            case 'X':
            case 'Y':
            case 'Z':
                if (ctx->command.data == NULL) {
                    ctx->command.data = ngx_pnalloc(r->pool,
                            NGX_HTTP_XCGI_COMMAND_LEN);
                    if (ctx->command.data == NULL) {
                        return NGX_ERROR;
                    }
                }

                ctx->command.data[0] = ch;
                ctx->command.len = 1;

                state = xcgi_command_state;
                break;

            default:
                state = xcgi_error_state;
            }

            break;

        case xcgi_command_state:
            switch (ch) {

            case ' ':
            case CR:
            case LF:
            case '\t':
                state = xcgi_postcommand_state;
                break;

            case '_':
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'e':
            case 'f':
            case 'g':
            case 'h':
            case 'i':
            case 'j':
            case 'k':
            case 'l':
            case 'm':
            case 'n':
            case 'o':
            case 'p':
            case 'q':
            case 'r':
            case 's':
            case 't':
            case 'u':
            case 'v':
            case 'w':
            case 'x':
            case 'y':
            case 'z':
            case 'A':
            case 'B':
            case 'C':
            case 'D':
            case 'E':
            case 'F':
            case 'G':
            case 'H':
            case 'I':
            case 'J':
            case 'K':
            case 'L':
            case 'M':
            case 'N':
            case 'O':
            case 'P':
            case 'Q':
            case 'R':
            case 'S':
            case 'T':
            case 'U':
            case 'V':
            case 'W':
            case 'X':
            case 'Y':
            case 'Z':
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                if (ctx->command.len == NGX_HTTP_XCGI_COMMAND_LEN) {
                    state = xcgi_error_state;
                } else {
                    ctx->command.data[ctx->command.len++] = ch;
                }
                break;

            case '(':
                state = xcgi_preparam_state;
                break;

            default:
                    state = xcgi_error_state;
                    break;
            }

            break;

        case xcgi_postcommand_state:
            switch (ch) {

            case ' ':
            case '\r':
            case '\n':
            case '\t':
                break;

            case '(':
                state = xcgi_preparam_state;
                break;

            default:
                state = xcgi_error_state;
                break;
            }

            break;

        case xcgi_preparam_state:
            switch (ch) {

            case ' ':
            case '\r':
            case '\n':
            case '\t':
                break;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                state = xcgi_intparam_state;
                ctx->argv[ctx->argc][ctx->argvlen[ctx->argc]++] = ch;
                break;

            case '\"':
                state = xcgi_stringparam_state;
                break;

            case ')':
                state = xcgi_postparam_state;
                break;

            default:
                state = xcgi_error_state;
                break;
            }
            break;

        case xcgi_intparam_state:
            switch (ch) {

            case ' ':
            case '\r':
            case '\n':
            case '\t':
                ctx->argc++;
                state = xcgi_postintparam_state;
                break;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                if (ctx->argvlen[ctx->argc] >= NGX_HTTP_XCGI_PARAM_LEN) {
                    state = xcgi_error_state;
                } else {
                    ctx->argv[ctx->argc][ctx->argvlen[ctx->argc]++] = ch;
                }
                break;

            case ')':
                state = xcgi_postparam_state;
                ctx->argc++;
                break;

            case ',':
                state = xcgi_nextparam_state;
                ctx->argc++;
                if (ctx->argc >= NGX_HTTP_XCGI_PARAM_COUNT) {
                    state = xcgi_error_state;
                }
                break;

            default:
                state = xcgi_error_state;
                break;
            }

            break;

        case xcgi_postintparam_state:
        case xcgi_poststringparam_state:
            switch (ch) {

            case ' ':
            case '\r':
            case '\n':
            case '\t':
                break;

            case ')':
                state = xcgi_postparam_state;
                break;

            case ',':
                state = xcgi_nextparam_state;
                if (ctx->argc >= NGX_HTTP_XCGI_PARAM_COUNT) {
                    state = xcgi_error_state;
                }
                break;

            default:
                state = xcgi_error_state;
                break;
            }
            break;

        case xcgi_nextparam_state:
            switch (ch) {

            case ' ':
            case '\r':
            case '\n':
            case '\t':
                break;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                state = xcgi_intparam_state;
                ctx->argv[ctx->argc][ctx->argvlen[ctx->argc]++] = ch;
                break;

            case '\"':
                state = xcgi_stringparam_state;
                break;

            default:
                state = xcgi_error_state;
                break;
            }
            break;

        case xcgi_stringparam_state:
            switch (ch) {
            case    '\\':
                state = xcgi_stringescape_state;
                break;

            case    '\"':
                state = xcgi_poststringparam_state;
                ctx->argc++;
                break;

            default:
                if (ctx->argvlen[ctx->argc] >= NGX_HTTP_XCGI_PARAM_LEN) {
                    state = xcgi_error_state;
                } else {
                    ctx->argv[ctx->argc][ctx->argvlen[ctx->argc]++] = ch;
                }
                break;
            }
            break;

        case xcgi_stringescape_state:
            if (ctx->argvlen[ctx->argc] >= NGX_HTTP_XCGI_PARAM_LEN) {
                state = xcgi_error_state;
                break;
            }

            switch (ch) {
            case '\\':
            case '\'':
            case '\"':
                state = xcgi_stringparam_state;
                break;

            case 't':
                ch = '\t';
                state = xcgi_stringparam_state;
                break;

            case 'r':
                ch = '\r';
                state = xcgi_stringparam_state;
                break;

            case 'n':
                ch = '\n';
                state = xcgi_stringparam_state;
                break;

            default:
                state = xcgi_error_state;
                break;
            }

            if (xcgi_stringparam_state == state) {
                ctx->argv[ctx->argc][ctx->argvlen[ctx->argc]++] = ch;
            }
            break;

        case xcgi_postparam_state:
            switch (ch) {

            case ';':
            case ' ':
            case '\r':
            case '\n':
            case '\t':
                break;

            case '%':
                state = xcgi_end_state;
                break;

            default:
                state = xcgi_error_state;
                break;

            }
            break;

        case xcgi_end_state:
            switch (ch) {
            case '>':
                ctx->state = xcgi_init_state;
                ctx->pos = p + 1;
                ctx->copy_end = copy_end;

                if (ctx->copy_start == NULL && copy_end) {
                    ctx->copy_start = ctx->buf->pos;
                }

                return NGX_OK;

            default:
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "unexpected \"%c\" symbol in \"%V\" XCGI command",
                              ch, &ctx->command);
                state = xcgi_error_state;
                break;
            }

            break;

        case xcgi_error_state:
            ngx_http_xcgi_ctx_init(r, ctx);
            switch (ch) {
            case '>':
                ctx->state = xcgi_init_state;
                ctx->pos = p + 1;
                ctx->copy_end = copy_end;

                if (ctx->copy_start == NULL && copy_end) {
                    ctx->copy_start = ctx->buf->pos;
                }

                return NGX_ERROR;

            default:
                state = xcgi_error_state;
                break;
            }

            break;
        }
    }

    ctx->state = state;
    ctx->pos = p;

    ctx->copy_end = (state == xcgi_init_state) ? p : copy_end;

    if (ctx->copy_start == NULL && ctx->copy_end) {
        ctx->copy_start = ctx->buf->pos;
    }

    return NGX_AGAIN;
}

static void ngx_xcgi_parse_post_data(ngx_http_request_t *r)
{
    int             start, and, equal, len;
    ngx_xcgi_handler_t *var;
    ngx_buf_t      *b = r->request_body->bufs->buf;

    if (!b) {
        return;
    }

    if (NULL == r->xcgi_var) {
        ngx_xcgi_init_xcgi_variable(r);
        if (NULL == r->xcgi_var) {
            return;
        }
    }

    len = b->last - b->pos;
    start = and = 0;
    while (and < len) {
        for (and = start; and < len; and++) {
            if ('&' == b->pos[and]) {
                break;
            }
        }

        for (equal = start; equal < and; equal++) {
            if ('=' == b->pos[equal]) {
                break;
            }
        }

        if (equal == and) {
            break;
        }

        if (and != equal+1) {
            var = ngx_pcalloc(r->pool, sizeof(ngx_xcgi_handler_t));
            if (NULL == var) {
                return;
            }

            var->name.data = b->pos + start;
            var->name.len = equal - start;
            var->data.value = ngx_palloc(r->pool, and-equal);
            if (NULL == var->data.value) {
                return;
            }
            memcpy(var->data.value, b->pos + equal+1, and - (equal+1));
            var->data.value[and - (equal+1)] = 0;

            ngx_xcgi_set_var_internal(r, var);
        }

        if (and == len) {
            break;

        }
        start = and+1;
    }
}

static void ngx_http_xcgi_xform_body(ngx_http_request_t *r)
{
    ngx_buf_t       *b;
    ngx_chain_t     out;
    ngx_int_t       rc;
    ngx_str_t       xform;

    if (!r->request_body || !r->request_body->bufs) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    ngx_xcgi_parse_post_data(r);

    xform.len = r->uri.len - s_xcgi_form_prefix.len;
    xform.data = r->uri.data + s_xcgi_form_prefix.len;

    b = ngx_create_temp_buf(r->pool, 4096);
    if (b == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }


    b->memory = 1;

    b->last_buf = 1;

    ngx_xcgi_call_handler(&xform, r, b, 0, NULL);

    out.buf = b;

    out.next = NULL;

    r->headers_out.content_type.len = sizeof("text/html") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html";

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = b->last - b->pos;

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        ngx_http_finalize_request(r, rc);
        return;
    }

    rc = ngx_http_output_filter(r, &out);
    if (r->main->count > 0) {
        r->main->count--;
    }
    ngx_http_finalize_request(r, rc);
}

static ngx_int_t ngx_http_xcgi_xform_handler(ngx_http_request_t *r)
{
    ngx_int_t   rc;

    r->headers_out.charset.data = (u_char *)"utf-8";
    r->headers_out.charset.len = 5;
    r->keepalive = 0;

    if (!(r->method & (NGX_HTTP_POST))) {
        return NGX_DECLINED;
    }

    if (r->uri.data[r->uri.len - 1] == '/') {
        return NGX_DECLINED;
    }

    if (r->uri.len <= s_xcgi_form_prefix.len ||
        ngx_strncmp(r->uri.data, s_xcgi_form_prefix.data, s_xcgi_form_prefix.len))
    {
        return NGX_DECLINED;
    }

    rc = ngx_http_read_client_request_body(r, ngx_http_xcgi_xform_body);
    if (rc != NGX_OK) {
        return rc;
    }

    return NGX_DONE;
}

static ngx_int_t ngx_http_xcgi_filter_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_xcgi_xform_handler;

    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_xcgi_header_filter;

    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_xcgi_body_filter;

    return NGX_OK;
}


static ngx_int_t ngx_http_xcgi_module_init(ngx_conf_t *cf)
{
    ngx_xcgi_private_init();

    return NGX_OK;
}


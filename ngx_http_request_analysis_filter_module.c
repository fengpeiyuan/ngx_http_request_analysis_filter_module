#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define DICT_IDX_SIZE 1777
#define DICT_BUCKET_SIZE 100

typedef struct {
    ngx_queue_t                      queue;
    ngx_int_t                        exact;
    ngx_str_t                        *name;
    u_char                            *file_name;
    ngx_int_t                        step;
    ngx_queue_t                      list;
} url_queue_t;

typedef struct url_tree_node_s url_tree_node_t;
struct url_tree_node_s {
    url_tree_node_t   *left;
    url_tree_node_t   *right;
    url_tree_node_t   *tree;
    ngx_int_t                        exact;
    ngx_int_t                        step;
    u_char                           len;
    u_char                           name[1];
};

typedef struct {
    ngx_int_t delay[2000];
    ngx_uint_t max_delay;
    ngx_int_t total_request;
    ngx_int_t critical_total_request;
    ngx_int_t status[600];
    ngx_int_t url_bucket_pos;
} ngx_http_request_analysis_stat_request;

typedef struct {
    char data[256];
    ngx_int_t next;
    ngx_int_t pre;
    ngx_int_t sequence;
} ngx_http_request_analysis_url_dict_bucket_item;

typedef struct {
    ngx_int_t bucket_idx[DICT_IDX_SIZE];
    ngx_int_t last_used_bucket;
    ngx_http_request_analysis_url_dict_bucket_item url_bucket_arr[DICT_BUCKET_SIZE];
} ngx_http_request_analysis_url_dict;

typedef struct {
    ngx_http_request_analysis_stat_request stat_bucket_arr[3][DICT_BUCKET_SIZE];
    ngx_int_t pid;
} ngx_http_request_analysis_stat_buffer;

typedef struct {
	ngx_http_request_analysis_url_dict url_dict;
    ngx_queue_t *url_queue;
    url_tree_node_t *url_tree;
} ngx_http_request_analysis_url_shctx_t;

typedef struct {
    ngx_int_t current_slots;
    ngx_int_t last_used_pos;
    ngx_http_request_analysis_stat_buffer *stat_buffer;
} ngx_http_request_analysis_stat_shctx_t;

typedef struct {
    ngx_http_request_analysis_url_shctx_t  *url_shm;
    ngx_slab_pool_t             *url_shpool;
    ngx_str_t url_file;
} ngx_http_request_analysis_url_ctx_t;

typedef struct {
    ngx_http_request_analysis_stat_shctx_t *stat_shm;
    ngx_slab_pool_t             *stat_shpool;
    ngx_int_t reset;
} ngx_http_request_analysis_stat_ctx_t;

typedef struct {
    ngx_slab_pool_t             *shpool;
} ngx_http_request_analysis_lock_ctx_t;

typedef struct {
    ngx_flag_t enable;
    ngx_str_t url_file;
    ngx_int_t request_init;
    ngx_int_t stat_buffer_pos;
    ngx_shm_zone_t *url_shm_zone;
    ngx_shm_zone_t *stat_shm_zone;
    ngx_shm_zone_t *stat_lock_shm_zone;
    ngx_shm_zone_t *url_lock_shm_zone;
} ngx_http_request_analysis_conf_t;

static ngx_int_t worker_processes;


static ngx_int_t add_url_queue(ngx_queue_t **url_queue, char *url, ngx_int_t exact_match, ngx_http_request_analysis_url_ctx_t *ctx, ngx_int_t step);
static ngx_int_t compare_url(const ngx_queue_t *one, const ngx_queue_t *two);
static void create_url_list(ngx_queue_t *url_queue, ngx_queue_t *q);
static url_tree_node_t * create_urls_tree(ngx_http_request_analysis_url_ctx_t *ctx, ngx_queue_t *url_queue, size_t prefix);
static ngx_int_t find_static_url(u_char *url, url_tree_node_t *node, ngx_str_t *path, ngx_int_t *step);
static void print_url_list(ngx_queue_t *qq);
static void print_url_tree(url_tree_node_t *node, ngx_int_t level);
static u_char* trim_url(u_char* url, ngx_int_t step);
static void destroy_url_queue(ngx_http_request_analysis_url_ctx_t *ctx, ngx_queue_t *q);
static void destroy_url_list(ngx_http_request_analysis_url_ctx_t *ctx, ngx_queue_t *q);
static void destroy_url_tree(ngx_http_request_analysis_url_ctx_t *ctx, url_tree_node_t *node);
static ngx_int_t ngx_http_request_analysis_filter_init (ngx_conf_t*);
static ngx_int_t ngx_http_request_analysis_header_filter(ngx_http_request_t*);
static char * ngx_http_request_analysis_merge_loc_conf(ngx_conf_t*, void*, void*);
static void * ngx_http_request_analysis_create_loc_conf(ngx_conf_t*);
static char * ngx_http_request_analysis_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_request_analysis_init_url_zone(ngx_shm_zone_t *shm_zone, void *data);
static ngx_int_t ngx_http_request_analysis_init_stat_zone(ngx_shm_zone_t *shm_zone, void *data);
static ngx_int_t ngx_http_request_analysis_init_lock_zone(ngx_shm_zone_t *shm_zone, void *data);
static ngx_int_t insert_url_into_dict(ngx_http_request_analysis_url_ctx_t *ctx, char *url);
static ngx_int_t search_url_from_dict(ngx_http_request_analysis_url_ctx_t *ctx, char *url);
static char *ngx_http_request_status(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_conf_post_handler_pt ngx_http_request_status_p = (ngx_conf_post_handler_pt)ngx_http_request_status;
static ngx_int_t ngx_http_request_status_handler(ngx_http_request_t *r);
static char *trim(char *str);
static ngx_int_t fill_url_shm_zone(ngx_http_request_analysis_url_ctx_t *ctx);

static ngx_command_t ngx_http_request_analysis_filter_commands[] = {
    { ngx_string("request_analysis_zone"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE4,
      ngx_http_request_analysis_zone,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },
    { ngx_string("request_status"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_http_request_status,
      0,
      0,
      &ngx_http_request_status_p },
      ngx_null_command
};

static ngx_http_module_t  ngx_http_request_analysis_filter_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_request_analysis_filter_init,
    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */
    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */
    ngx_http_request_analysis_create_loc_conf, /* create location configuration */
    ngx_http_request_analysis_merge_loc_conf   /* merge location configuration */
};

ngx_module_t  ngx_http_request_analysis_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_request_analysis_filter_ctx, /* module context */
    ngx_http_request_analysis_filter_commands,    /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_http_output_header_filter_pt ngx_http_next_header_filter;

static void *
ngx_http_request_analysis_create_loc_conf(ngx_conf_t *cf){
    ngx_http_request_analysis_conf_t  *conf;
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_request_analysis_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }
    conf->enable = NGX_CONF_UNSET;
    return conf;
}

static char *
ngx_http_request_analysis_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child){
    ngx_http_request_analysis_conf_t *prev = parent;
    ngx_http_request_analysis_conf_t *conf = child;
    if (conf->url_shm_zone == NULL) {
        conf->url_shm_zone = prev->url_shm_zone;
    }
    if (conf->stat_shm_zone == NULL) {
        conf->stat_shm_zone = prev->stat_shm_zone;
    }
    if (conf->stat_lock_shm_zone == NULL) {
        conf->stat_lock_shm_zone = prev->stat_lock_shm_zone;
    }
    if (conf->url_lock_shm_zone == NULL) {
        conf->url_lock_shm_zone = prev->url_lock_shm_zone;
    }
    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_str_value(conf->url_file, prev->url_file, "");
    return NGX_CONF_OK;
}

static char *
ngx_http_request_status(ngx_conf_t *cf, ngx_command_t *cmd, void *conf){
    ngx_http_core_loc_conf_t  *clcf;
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_request_status_handler;
    return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_request_status_handler(ngx_http_request_t *r){
    ngx_int_t rc;
    ngx_buf_t *b;
    ngx_chain_t out;
    size_t size;

    ngx_http_request_analysis_conf_t *racf;
    racf = ngx_http_get_module_loc_conf(r, ngx_http_request_analysis_filter_module);
    ngx_http_request_analysis_url_ctx_t  *url_ctx;
    url_ctx = racf->url_shm_zone->data;
    ngx_http_request_analysis_stat_ctx_t  *stat_ctx;
    stat_ctx = racf->stat_shm_zone->data;

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }
    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }
    r->headers_out.content_type_len = sizeof("text/plain") - 1;
    r->headers_out.content_type.data = (u_char *) "text/plain";
    if (r->method == NGX_HTTP_HEAD) {
        r->headers_out.status = NGX_HTTP_OK;
        r->headers_out.content_length_n = 0;
        return ngx_http_send_header(r);
    }
    size = 1024 * 1024;
    b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ngx_shmtx_lock(&((ngx_slab_pool_t*)(racf->stat_lock_shm_zone->shm.addr))->mutex);
    out.buf = b;
    out.next = NULL;

    ngx_int_t old_bno = stat_ctx->stat_shm->current_slots;
    ngx_int_t reset = stat_ctx->reset;
    stat_ctx->stat_shm->current_slots == 0 ? (stat_ctx->stat_shm->current_slots = 1) : (stat_ctx->stat_shm->current_slots = 0);
    ngx_int_t loop = 0;
    ngx_int_t stat_bno = old_bno;
    ngx_int_t wn;
    if (!reset) {
        for (wn = 0; wn < worker_processes; wn++) {
            ngx_http_request_analysis_stat_buffer *sb = stat_ctx->stat_shm->stat_buffer + wn;
            for (loop = 0; loop < DICT_BUCKET_SIZE; loop++) {
                if (sb->stat_bucket_arr[old_bno][loop].total_request <= 0)
                	continue;
                sb->stat_bucket_arr[2][loop].total_request += sb->stat_bucket_arr[old_bno][loop].total_request;
                sb->stat_bucket_arr[2][loop].max_delay < sb->stat_bucket_arr[old_bno][loop].max_delay ?(sb->stat_bucket_arr[2][loop].max_delay) = sb->stat_bucket_arr[old_bno][loop].max_delay : 0;
                sb->stat_bucket_arr[2][loop].critical_total_request += sb->stat_bucket_arr[old_bno][loop].critical_total_request;
                ngx_int_t status_code;
                for (status_code = 0; status_code < 600; status_code++) {
                    sb->stat_bucket_arr[2][loop].status[status_code] += sb->stat_bucket_arr[old_bno][loop].status[status_code];
                }
                ngx_int_t d;
                for (d = 0; d < 2000; d++) {
                    sb->stat_bucket_arr[2][loop].delay[d] += sb->stat_bucket_arr[old_bno][loop].delay[d];
                }
            }
        }
        stat_bno = 2;
    }

    b->last = ngx_sprintf(b->last, "url\t\t\ttotal\tmax\ttp99\ttp999\tcritical\tstatus\n");
    ngx_int_t url_idx;
    for (url_idx = 0; url_idx < DICT_BUCKET_SIZE; url_idx++) {
        if (strlen(url_ctx->url_shm->url_dict.url_bucket_arr[url_idx].data) == 0) {
            continue;
        }
        ngx_int_t total_request_per_url = 0;
        ngx_int_t serious_delay_request_per_url = 0;
        ngx_uint_t max_delay = 0;
        ngx_int_t tp99_request_per_url = 0;
        ngx_int_t tp999_request_per_url = 0;
        ngx_int_t tp99_per_url = 0;
        ngx_int_t tp999_per_url = 0;
        ngx_int_t status[600] = {0};
        for (wn = 0; wn < worker_processes; wn++) {
            ngx_http_request_analysis_stat_buffer *sb = stat_ctx->stat_shm->stat_buffer + wn;
            total_request_per_url += sb->stat_bucket_arr[stat_bno][url_idx].total_request;
            serious_delay_request_per_url += sb->stat_bucket_arr[stat_bno][url_idx].critical_total_request;
            sb->stat_bucket_arr[stat_bno][url_idx].max_delay > max_delay ? max_delay = sb->stat_bucket_arr[stat_bno][url_idx].max_delay : 0;
            ngx_int_t code;
            for (code = 0; code < 600; code++) {
                status[code] += sb->stat_bucket_arr[stat_bno][url_idx].status[code];
            }
        }
        tp99_request_per_url = total_request_per_url * 0.99;
        tp999_request_per_url = total_request_per_url * 0.999;
        ngx_int_t d = 0;
        ngx_int_t total_delay_request_per_url = 0;
        ngx_int_t tp99_finished = 0;
        ngx_int_t tp999_finished = 0;
        for (d = 0; d < 2000 && !tp999_finished ; d++) {
            for (wn = 0; wn < worker_processes; wn++) {
                ngx_http_request_analysis_stat_buffer *sb = stat_ctx->stat_shm->stat_buffer + wn;
                total_delay_request_per_url += sb->stat_bucket_arr[stat_bno][url_idx].delay[d];
                if (total_delay_request_per_url >= tp99_request_per_url && !tp99_finished) {
                	tp99_per_url = d;
                    tp99_finished = 1;
                }
                
                if (total_delay_request_per_url >= tp999_request_per_url && !tp999_finished) {
                	tp999_per_url = d;
                    tp999_finished = 1;
                }
            }
        }
        char stp99[32];
        ngx_memset(stp99, 0 ,sizeof(stp99));
        !tp99_finished ? sprintf(stp99, "2000+") : sprintf(stp99, "%d", (int)tp99_per_url);
        char stp999[32];
        ngx_memset(stp999, 0, sizeof(stp999));
        !tp999_finished ? sprintf(stp999, "2000+") : sprintf(stp999, "%d", (int)tp999_per_url);

        u_char status_buf[1024];
        ngx_memset(status_buf, 0, sizeof(status_buf));
        ngx_int_t code_idx;
        for (code_idx = 0; code_idx < 600; code_idx++) {
            if (status[code_idx] > 0) {
                ngx_sprintf(status_buf, "code:%i,num:%i|", code_idx, status[code_idx]);
            }
        }
        status_buf[ngx_strlen(status_buf) - 1] = '\0';
        b->last = ngx_sprintf(b->last, "%s\t\t\t%i\t%i\t%s\t%s\t%i\t%s\n",url_ctx->url_shm->url_dict.url_bucket_arr[url_idx].data,total_request_per_url, max_delay, stp99, stp999, serious_delay_request_per_url, status_buf);
    }
    ngx_int_t wp;
    for (wp = 0; wp < worker_processes; wp++) {
        ngx_http_request_analysis_stat_buffer *sb = stat_ctx->stat_shm->stat_buffer + wp;
        ngx_memset(sb->stat_bucket_arr[old_bno], 0,DICT_BUCKET_SIZE * sizeof(ngx_http_request_analysis_stat_request));
    }
    ngx_shmtx_unlock(&((ngx_slab_pool_t*)(racf->stat_lock_shm_zone->shm.addr))->mutex);
    b->memory = 1;
    b->last_buf = 1;
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = b->last - b->pos;
    b->last_buf = (r == r->main) ? 1 : 0;
    b->last_in_chain = 1;
    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, &out);
}

static ngx_int_t
ngx_http_request_analysis_header_filter(ngx_http_request_t *r){
    struct timeval tv;
    ngx_uint_t now_sec;
    ngx_uint_t req_sec;
    ngx_uint_t now_msec;
    ngx_uint_t req_msec;
    ngx_uint_t delay_msec;
    char uri_buf[256];
    ngx_http_request_analysis_conf_t *racf;
    ngx_http_request_analysis_url_ctx_t  *url_ctx;
    ngx_http_request_analysis_stat_ctx_t  *stat_ctx;
    racf = ngx_http_get_module_loc_conf(r, ngx_http_request_analysis_filter_module);
    url_ctx = racf->url_shm_zone->data;
    stat_ctx = racf->stat_shm_zone->data;
    snprintf(uri_buf, r->unparsed_uri.len + 1, "%s", r->unparsed_uri.data);
    u_char path_buf[256];
    ngx_str_t url;
    url.data = path_buf;
    url.len = 0;
    ngx_int_t rt;
    ngx_int_t step = 0;
    rt = find_static_url((u_char*)uri_buf, url_ctx->url_shm->url_tree, &url, &step);
    ngx_int_t idx;
    if (rt == NGX_OK) {
    	idx = search_url_from_dict(racf->url_shm_zone->data, (char *)uri_buf);
        if (idx < 0) {
             ngx_shmtx_lock(&((ngx_slab_pool_t*)(racf->url_lock_shm_zone->shm.addr))->mutex);
             idx = insert_url_into_dict(racf->url_shm_zone->data, uri_buf);
             ngx_shmtx_unlock(&((ngx_slab_pool_t*)(racf->url_lock_shm_zone->shm.addr))->mutex);
        }
    } else if (rt == NGX_AGAIN) {
        if (url.len == r->unparsed_uri.len || (url.len < r->unparsed_uri.len && r->unparsed_uri.data[url.len] == '/')) {
            u_char *p = trim_url((u_char *)uri_buf, step);
            idx = search_url_from_dict(racf->url_shm_zone->data, (char *)p);
            if (idx < 0) {
                 ngx_shmtx_lock(&((ngx_slab_pool_t*)(racf->url_lock_shm_zone->shm.addr))->mutex);
                 idx = insert_url_into_dict(racf->url_shm_zone->data, (char *)p);
                 ngx_shmtx_unlock(&((ngx_slab_pool_t*)(racf->url_lock_shm_zone->shm.addr))->mutex);
            }
        } else {
            return ngx_http_next_header_filter(r);
        }
    } else {
        return ngx_http_next_header_filter(r);
    }

    if (idx < 0) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"the url url_bucket_arr full filled, some url_queue met condition cann't be counted!");
        return ngx_http_next_header_filter(r);
    }
    
    if (racf->request_init != 1) {
        ngx_shmtx_lock(&((ngx_slab_pool_t*)(racf->url_lock_shm_zone->shm.addr))->mutex);
        if (racf->request_init != 1) {
        	racf->request_init = 1;
            if (stat_ctx->stat_shm->last_used_pos >= worker_processes) {
                char buf[16] = {0};
                ngx_int_t *pid;
                ngx_pool_t *pool = NULL;
                pool = r->pool;
                ngx_array_t* arr = ngx_array_create(pool, worker_processes, sizeof(ngx_int_t));
                FILE *fp = popen("ps -ef | grep 'nginx' | grep -i worker | awk \'{print $2}\'", "r");
                if (fp == NULL) {
                    return NGX_ERROR;
                }
                while (NULL != fgets(buf, sizeof(buf), fp)) {
                    pid = (ngx_int_t*)ngx_array_push(arr);
                    *pid = atoi(buf);
                }
                ngx_uint_t i;
                ngx_int_t j;
                for (j = 0; j < worker_processes; j++) {
                    ngx_http_request_analysis_stat_buffer *sb = stat_ctx->stat_shm->stat_buffer + j;
                    for (i = 0; i < arr->nelts; ++i) {
                        pid = (ngx_int_t*)((ngx_int_t*)arr->elts + i);
                        if (*pid == sb->pid) {
                            break;
                        }
                    }
                    if (i >= arr->nelts) {
                    	racf->stat_buffer_pos = j;
                        sb->pid = getpid();
                        break;
                    }
                }
                pclose(fp);
            } else {
            	racf->stat_buffer_pos = stat_ctx->stat_shm->last_used_pos;
                ngx_http_request_analysis_stat_buffer *sb = stat_ctx->stat_shm->stat_buffer + racf->stat_buffer_pos;
                sb->pid = getpid();
            }
            stat_ctx->stat_shm->last_used_pos++;
        }
        ngx_shmtx_unlock(&((ngx_slab_pool_t*)(racf->url_lock_shm_zone->shm.addr))->mutex);
    }

    ngx_http_request_analysis_stat_buffer *curr_sb = stat_ctx->stat_shm->stat_buffer + racf->stat_buffer_pos;
    curr_sb->stat_bucket_arr[stat_ctx->stat_shm->current_slots][idx].total_request++;
    curr_sb->stat_bucket_arr[stat_ctx->stat_shm->current_slots][idx].status[r->headers_out.status]++;

    ngx_gettimeofday(&tv);
    now_sec = tv.tv_sec;
    now_msec = tv.tv_usec / 1000;
    req_sec = r->start_sec;
    req_msec = r->start_msec;
    delay_msec = ((now_sec * 1000) + now_msec) - ((req_sec * 1000) + req_msec);
    if (delay_msec >= 2000) {
    	curr_sb->stat_bucket_arr[stat_ctx->stat_shm->current_slots][idx].critical_total_request++;
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"--- serious delay happend, %i ms consumed --- ", delay_msec);
        return ngx_http_next_header_filter(r);
    }
    curr_sb->stat_bucket_arr[stat_ctx->stat_shm->current_slots][idx].delay[delay_msec]++;
    curr_sb->stat_bucket_arr[stat_ctx->stat_shm->current_slots][idx].url_bucket_pos = idx;
    delay_msec > curr_sb->stat_bucket_arr[stat_ctx->stat_shm->current_slots][idx].max_delay ? curr_sb->stat_bucket_arr[stat_ctx->stat_shm->current_slots][idx].max_delay = delay_msec : 0;
    return ngx_http_next_header_filter(r);
}

static char *
ngx_http_request_analysis_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf){
    u_char                    *p;
    ssize_t                    url_size;
    ssize_t                    stat_size;
    ngx_str_t                 *value, url_name, stat_name, s;
    ngx_uint_t                 i;
    ngx_shm_zone_t            *url_shm_zone;
    ngx_shm_zone_t            *stat_shm_zone;
    ngx_shm_zone_t            *stat_lock_shm_zone;
    ngx_shm_zone_t            *url_lock_shm_zone;
    ngx_http_request_analysis_url_ctx_t  *url_ctx;
    ngx_http_request_analysis_stat_ctx_t *stat_ctx;
    ngx_http_request_analysis_lock_ctx_t *request_lock;
    ngx_http_request_analysis_lock_ctx_t *stat_lock;
    ngx_core_conf_t *ccf = (ngx_core_conf_t *) ngx_get_conf(cf->cycle->conf_ctx, ngx_core_module);
    ngx_http_request_analysis_conf_t *lrsl = conf;

    worker_processes = ccf->worker_processes;
    value = cf->args->elts;
    url_ctx = NULL;
    stat_ctx = NULL;
    url_size = 0;
    url_name.len = 0;
    stat_name.len = 0;
    ngx_int_t reset = 0;
    stat_lock = NULL;
    request_lock = NULL;
    stat_size = 0;

    for (i = 1; i < cf->args->nelts; i++) {        
        if (ngx_strncmp(value[i].data, "url_zone=", 9) == 0) {
            url_name.data = value[i].data + 9;
            p = (u_char *) ngx_strchr(url_name.data, ':');
            if (p == NULL) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"invalid zone size \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }
            url_name.len = p - url_name.data;
            s.data = p + 1;
            s.len = value[i].data + value[i].len - s.data;
            url_size = ngx_parse_size(&s);
            if (url_size == NGX_ERROR) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"invalid zone size \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }
            if (url_size < (ssize_t) (8 * ngx_pagesize)) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"zone \"%V\" is too small", &value[i]);
                return NGX_CONF_ERROR;
            }
            continue;
        }

        if (ngx_strncmp(value[i].data, "stat_zone=", 10) == 0) {
            stat_name.data = value[i].data + 10;
            stat_name.len = value[i].len - 10;
            p = (u_char *) ngx_strchr(stat_name.data, ':');
            if (p == NULL) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"invalid zone size \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }
            s.data = p + 1;
            s.len = value[i].data + value[i].len - s.data;
            stat_size = ngx_parse_size(&s);
            if (stat_size == NGX_ERROR) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"invalid zone size \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }
            if (stat_size < (ssize_t) (8 * ngx_pagesize)) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"zone \"%V\" is too small", &value[i]);
                return NGX_CONF_ERROR;
            }
            if (stat_size < (ssize_t) (worker_processes * sizeof(ngx_http_request_analysis_stat_buffer))) {
            	ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"stat zone \"%V\" is too small,at lease %d", &value[i],worker_processes * sizeof(ngx_http_request_analysis_stat_buffer));
            	return NGX_CONF_ERROR;
            }
            continue;
        }

        if (ngx_strncmp(value[i].data, "url_file=", 9) == 0) {
            value[i].data += 9;
            value[i].len -= 9;
            url_ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_request_analysis_url_ctx_t));
            if (url_ctx == NULL) {
                return NGX_CONF_ERROR;
            }
            url_ctx->url_file = value[i];
            continue;
        }

        if (ngx_strncmp(value[i].data, "reset=", 6) == 0) {
            value[i].data += 6;
            value[i].len -= 6;
            if (value[i].len == ngx_strlen("true") && ngx_strncmp(value[i].data, "true", ngx_strlen("true")) == 0) {
                reset = 1;
            } else if (value[i].len == ngx_strlen("false") && ngx_strncmp(value[i].data, "false", ngx_strlen("false")) == 0) {
                reset = 0;
            } else {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"invalid parameter \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }
            continue;
        }
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"invalid parameter \"%V\"", &value[i]);
        return NGX_CONF_ERROR;
    }

    char buf[256];
    snprintf(buf, url_ctx->url_file.len + 1, "%s", url_ctx->url_file.data);
    FILE *fh = fopen(buf, "r");
    if (fh == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"failed to open url configure file '%s'.\n", buf);
        return NGX_CONF_ERROR;
    }
    if (url_name.len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"\"%V\" must have \"zone\" parameter",&cmd->name);
        return NGX_CONF_ERROR;
    }

    //stat_lock
    stat_lock = ngx_pcalloc(cf->pool, sizeof(ngx_http_request_analysis_lock_ctx_t));
    if (stat_lock == NULL) {
        	return NGX_CONF_ERROR;
    }
    ngx_str_t stat_lock_name = ngx_string("stat_lock");
    stat_lock_shm_zone = ngx_shared_memory_add(cf, &stat_lock_name, 16 * ngx_pagesize,&ngx_http_request_analysis_filter_module);
    if (stat_lock_shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }
    if (stat_lock_shm_zone->data) {
        stat_lock = stat_lock_shm_zone->data;
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"%V \"%V\" is already bound to variable \"%V\"",&cmd->name, &url_name, &url_ctx->url_file);
        return NGX_CONF_ERROR;
    }
    stat_lock_shm_zone->init = ngx_http_request_analysis_init_lock_zone;
    stat_lock_shm_zone->data = stat_lock;
    lrsl->stat_lock_shm_zone = stat_lock_shm_zone;

    //request_lock
    request_lock = ngx_pcalloc(cf->pool, sizeof(ngx_http_request_analysis_lock_ctx_t));
    if (request_lock == NULL) {
       	return NGX_CONF_ERROR;
    }
    ngx_str_t request_lock_name = ngx_string("request_lock");
    url_lock_shm_zone = ngx_shared_memory_add(cf, &request_lock_name, 16 * ngx_pagesize,&ngx_http_request_analysis_filter_module);
    if (url_lock_shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }
    if (url_lock_shm_zone->data) {
        request_lock = url_lock_shm_zone->data;
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"%V \"%V\" is already bound to variable \"%V\"",&cmd->name, &url_name, &url_ctx->url_file);
        return NGX_CONF_ERROR;
    }
    url_lock_shm_zone->init = ngx_http_request_analysis_init_lock_zone;
    url_lock_shm_zone->data = request_lock;
    lrsl->url_lock_shm_zone = url_lock_shm_zone;
    
    //url_ctx
    url_shm_zone = ngx_shared_memory_add(cf, &url_name, url_size,&ngx_http_request_analysis_filter_module);
    if (url_shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }
    if (url_shm_zone->data) {
        url_ctx = url_shm_zone->data;
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"%V \"%V\" is already bound to variable \"%V\"",&cmd->name, &url_name, &url_ctx->url_file);
        return NGX_CONF_ERROR;
    }
    url_shm_zone->init = ngx_http_request_analysis_init_url_zone;
    url_shm_zone->data = url_ctx;
    lrsl->url_shm_zone = url_shm_zone;

    //stat_ctx
    stat_ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_request_analysis_stat_ctx_t));
    if (stat_ctx == NULL) {
       	return NGX_CONF_ERROR;
    }
    if (stat_name.len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"\"%V\" must have \"zone\" parameter",&cmd->name);
        return NGX_CONF_ERROR;
    }
    stat_shm_zone = ngx_shared_memory_add(cf, &stat_name, stat_size,&ngx_http_request_analysis_filter_module);
    if (stat_shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }
    if (stat_shm_zone->data) {
        stat_ctx = stat_shm_zone->data;
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"%V \"%V\" is already bound to variable \"%V\"",&cmd->name, &stat_name, &url_ctx->url_file);
        return NGX_CONF_ERROR;
    }
    stat_shm_zone->init = ngx_http_request_analysis_init_stat_zone;
    stat_shm_zone->data = stat_ctx;
    stat_ctx->reset = reset;
    lrsl->stat_shm_zone = stat_shm_zone;
    return NGX_CONF_OK;

}

static ngx_int_t
ngx_http_request_analysis_filter_init (ngx_conf_t *cf){
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_request_analysis_header_filter;
    return NGX_OK;
}

static ngx_int_t
ngx_http_request_analysis_init_url_zone (ngx_shm_zone_t *shm_zone, void *data){
    ngx_http_request_analysis_url_ctx_t  *octx = data;
    size_t                     len;
    ngx_http_request_analysis_url_ctx_t  *ctx;
    ctx = shm_zone->data;

    if (octx) {
        ctx->url_shm = octx->url_shm;
        ctx->url_shpool = octx->url_shpool;
        destroy_url_queue(ctx, ctx->url_shm->url_queue);
        destroy_url_tree(ctx, ctx->url_shm->url_tree);
        fill_url_shm_zone(ctx);
        ngx_queue_sort(ctx->url_shm->url_queue, compare_url);
        create_url_list(ctx->url_shm->url_queue, ngx_queue_head(ctx->url_shm->url_queue));
        print_url_list(ctx->url_shm->url_queue);
        ctx->url_shm->url_tree = create_urls_tree(ctx, ctx->url_shm->url_queue, 0);
        print_url_tree(ctx->url_shm->url_tree, 0);
        return NGX_OK;
    }
    ctx->url_shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;
    if (shm_zone->shm.exists) {
        ctx->url_shm = ctx->url_shpool->data;
        ctx->url_shpool = octx->url_shpool;
        destroy_url_queue(ctx, ctx->url_shm->url_queue);
        destroy_url_tree(ctx, ctx->url_shm->url_tree);
        fill_url_shm_zone(ctx);
        ngx_queue_sort(ctx->url_shm->url_queue, compare_url);
        create_url_list(ctx->url_shm->url_queue, ngx_queue_head(ctx->url_shm->url_queue));
        print_url_list(ctx->url_shm->url_queue);
        ctx->url_shm->url_tree = create_urls_tree(ctx, ctx->url_shm->url_queue, 0);
        print_url_tree(ctx->url_shm->url_tree, 0);
        fill_url_shm_zone(ctx);
        return NGX_OK;
    }

    ctx->url_shm = ngx_slab_alloc(ctx->url_shpool, sizeof(ngx_http_request_analysis_url_shctx_t));
    if (ctx->url_shm == NULL) {
        return NGX_ERROR;
    }
    ngx_memset(ctx->url_shm, 0, sizeof(ngx_http_request_analysis_url_shctx_t));
    ngx_int_t loop;
    for (loop = 0; loop < DICT_IDX_SIZE; loop++) {
        ctx->url_shm->url_dict.bucket_idx[loop] = -1;
    }
    ctx->url_shm->url_dict.last_used_bucket = -1;
    for (loop = 0; loop < DICT_BUCKET_SIZE; loop++) {
        ctx->url_shm->url_dict.url_bucket_arr[loop].next = -1;
    }

    ctx->url_shpool->data = ctx->url_shm;

    len = sizeof(" in log request speed zone \"\"") + shm_zone->shm.name.len;
    ctx->url_shpool->log_ctx = ngx_slab_alloc(ctx->url_shpool, len);
    if (ctx->url_shpool->log_ctx == NULL) {
        return NGX_ERROR;
    }
    ngx_sprintf(ctx->url_shpool->log_ctx, " in log request speed zone \"%V\"%Z",&shm_zone->shm.name);
    ctx->url_shpool->log_nomem = 0;

    fill_url_shm_zone(ctx);

    ngx_queue_sort(ctx->url_shm->url_queue, compare_url);
    create_url_list(ctx->url_shm->url_queue, ngx_queue_head(ctx->url_shm->url_queue));
    //print_url_list(ctx->url_shm->url_queue);
    ctx->url_shm->url_tree = create_urls_tree(ctx, ctx->url_shm->url_queue, 0);

    //print_url_tree(ctx->url_shm->url_tree, 0);
    return NGX_OK;
}

static ngx_int_t
ngx_http_request_analysis_init_stat_zone (ngx_shm_zone_t *shm_zone, void *data){
    ngx_http_request_analysis_stat_ctx_t  *octx = data;
    size_t                     len;
    ngx_http_request_analysis_stat_ctx_t  *ctx;
    ctx = shm_zone->data;

    if (octx) {
        ctx->stat_shm = octx->stat_shm;
        ctx->stat_shpool = octx->stat_shpool;
        return NGX_OK;
    }

    ctx->stat_shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;
    if (shm_zone->shm.exists) {
        ctx->stat_shm = ctx->stat_shpool->data;
        return NGX_OK;
    }
    ctx->stat_shm = ngx_slab_alloc(ctx->stat_shpool, sizeof(ngx_http_request_analysis_stat_shctx_t));
    if (ctx->stat_shm == NULL) {
        return NGX_ERROR;
    }

    ctx->stat_shm->stat_buffer = ngx_slab_alloc(ctx->stat_shpool, worker_processes * sizeof(ngx_http_request_analysis_stat_buffer));
    if (ctx->stat_shm->stat_buffer == NULL) {
        return NGX_ERROR;
    }
    ctx->stat_shm->last_used_pos = 0;
    
    ctx->stat_shpool->data = ctx->stat_shm;
    len = sizeof(" in log request speed zone \"\"") + shm_zone->shm.name.len;
    ctx->stat_shpool->log_ctx = ngx_slab_alloc(ctx->stat_shpool, len);
    if (ctx->stat_shpool->log_ctx == NULL) {
        return NGX_ERROR;
    }
    ngx_sprintf(ctx->stat_shpool->log_ctx, " in log request speed zone \"%V\"%Z", &shm_zone->shm.name);
    ctx->stat_shpool->log_nomem = 0;

    return NGX_OK;
}

static ngx_int_t
ngx_http_request_analysis_init_lock_zone (ngx_shm_zone_t *shm_zone, void *data){
    ngx_http_request_analysis_lock_ctx_t  *olock = data;
    ngx_http_request_analysis_lock_ctx_t  *lock;
    lock = shm_zone->data;
    if (olock) {
        lock->shpool = olock->shpool;
        return NGX_OK;
    }
    lock->shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;
    if (shm_zone->shm.exists) {
        lock->shpool = lock->shpool->data;
        return NGX_OK;
    }
    return NGX_OK;
}

static ngx_int_t
insert_url_into_dict(ngx_http_request_analysis_url_ctx_t  *ctx, char *url){
    ngx_http_request_analysis_url_shctx_t *shm;
    shm = ctx->url_shm;
    ngx_uint_t key;
    key = ngx_hash_key_lc((u_char *)url, strlen(url)) % DICT_IDX_SIZE;
    ngx_int_t idx = shm->url_dict.bucket_idx[key];
    ngx_int_t last_used_bucket = shm->url_dict.last_used_bucket;
    ngx_int_t pre_idx = idx;
    if (last_used_bucket >= DICT_BUCKET_SIZE - 1) {
        return -1;
    }
    if (idx >= 0) {
        while (idx >= 0 && strcasecmp(shm->url_dict.url_bucket_arr[idx].data, url)) {
            pre_idx = idx;
            idx = shm->url_dict.url_bucket_arr[idx].next;
        }
        if (idx >= 0) {
            return -idx;
        } else {
            strcpy(shm->url_dict.url_bucket_arr[++last_used_bucket].data, url);
            shm->url_dict.url_bucket_arr[pre_idx].next = last_used_bucket;
        }
    } else {
        shm->url_dict.bucket_idx[key] = ++last_used_bucket;
        strcpy(shm->url_dict.url_bucket_arr[last_used_bucket].data, url);
    }
    shm->url_dict.last_used_bucket = last_used_bucket;
    return last_used_bucket;
}

static ngx_int_t
search_url_from_dict(ngx_http_request_analysis_url_ctx_t  *ctx, char *url){
    ngx_http_request_analysis_url_shctx_t *shm;
    shm= ctx->url_shm;
    ngx_uint_t key;
    key = ngx_hash_key_lc((u_char *)url, strlen(url)) % DICT_IDX_SIZE;
    ngx_int_t idx = shm->url_dict.bucket_idx[key];
    if (idx >= 0) {
        while (idx >= 0 && strcasecmp(shm->url_dict.url_bucket_arr[idx].data, url)) {
            idx = shm->url_dict.url_bucket_arr[idx].next;
        }
        if (idx >= 0) {
            return idx;
        } else {
            return -1;
        }
    } else {
        return -1;
    }
}

static char *
trim(char *str){
    char *end;
    while(isspace(*str)) str++;
    if(*str == 0)
        return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace(*end)) end--;
    *(end+1) = 0;
    return str;
}

static ngx_int_t
fill_url_shm_zone(ngx_http_request_analysis_url_ctx_t *ctx){
    char buf[256];
    char *p;
    snprintf(buf, ctx->url_file.len + 1, "%s", ctx->url_file.data);
    FILE *fh = fopen(buf, "r");
    if (fh == NULL) {
        printf ("failed to open url configure file '%s'.\n", buf);
        return NGX_ERROR;
    }
    char c_equa = '=';
    ngx_int_t urlnum = 0;
    while (NULL != fgets(buf, sizeof(buf), fh)) {
        if (strlen(buf) > 0) {
            if (++urlnum > DICT_BUCKET_SIZE) {
                printf ("configured url number beyond the permitted limit %d\n", DICT_BUCKET_SIZE);
                return NGX_ERROR;
            }
            ngx_http_request_analysis_url_shctx_t *sh;
            sh = ctx->url_shm;
            char delims[] = " \t";
            char *result = NULL;
            char url[256];
            ngx_int_t step = 0;
            ngx_int_t pos = 0;
            result = strtok(buf, delims);
            while( result != NULL ) {
                if (pos == 0) {
                    snprintf(url, strlen(result) + 1, "%s", result);
                } else if (pos == 1) {
                    step = atoi(result);
                    step <= 0 ? (step = 0) : step;
                }
                result = strtok(NULL, delims);
                pos++;
            }
            ngx_int_t exact;
            p = trim(url);
            if(*p == c_equa){
            	exact = 1;
            	p++;
            }else{
            	 exact = 0;
            }
            add_url_queue(&sh->url_queue, p, exact, ctx, step);
        }
    }
    fclose(fh);
    return NGX_OK;
}


static ngx_int_t
compare_url(const ngx_queue_t *one, const ngx_queue_t *two){
    url_queue_t  *uq1, *uq2;
    uq1 = (url_queue_t *) one;
    uq2 = (url_queue_t *) two;

    return ngx_filename_cmp(uq1->name->data, uq2->name->data,ngx_min(uq1->name->len, uq2->name->len) + 1);
}

static ngx_int_t
add_url_queue(ngx_queue_t **url_queue, char *url, ngx_int_t exact_match, ngx_http_request_analysis_url_ctx_t *ctx, ngx_int_t step)
{
    url_queue_t  *urlq;
    ngx_slab_pool_t *pool = ctx->url_shpool;
    if (*url_queue == NULL) {
        *url_queue = ngx_slab_alloc(pool,sizeof(url_queue_t));
        if (*url_queue == NULL) {
            return NGX_ERROR;
        }
        ngx_queue_init(*url_queue);
    }
    urlq = ngx_slab_alloc(pool, sizeof(url_queue_t));
    if (urlq == NULL) {
        return NGX_ERROR;
    }

    if (exact_match){
    	urlq->exact = 1;
    } else {
    	urlq->exact = 0;
    }

    urlq->name = ngx_slab_alloc(pool, sizeof(ngx_str_t));
    urlq->name->data = ngx_slab_alloc(pool, ngx_strlen(url));
    urlq->name->len = ngx_strlen(url);
    urlq->step = step;
    ngx_memcpy(urlq->name->data, (u_char*)url, ngx_strlen(url));

    ngx_queue_init(&urlq->list);

    ngx_queue_insert_tail(*url_queue, &urlq->queue);

    return NGX_OK;
}

static void
create_url_list(ngx_queue_t *url_queue, ngx_queue_t *q)
{
    u_char                     *name;
    size_t                      len;
    ngx_queue_t                *x, tail;
    url_queue_t  *uq, *ux;

    if (q == ngx_queue_last(url_queue)) {
        return;
    }

    uq = (url_queue_t *) q;
    if (uq->exact) {
        create_url_list(url_queue, ngx_queue_next(q));
        return;
    }

    len = uq->name->len;
    name = uq->name->data;

    for (x = ngx_queue_next(q);x != ngx_queue_sentinel(url_queue);x = ngx_queue_next(x)){
        ux = (url_queue_t *) x;
        if (len > ux->name->len || ngx_filename_cmp(name, ux->name->data, len) != 0) {
            break;
        }
    }

    q = ngx_queue_next(q);

    if (q == x) {
        create_url_list(url_queue, x);
        return;
    }

    ngx_queue_split(url_queue, q, &tail);
    ngx_queue_add(&uq->list, &tail);
    
    if (x == ngx_queue_sentinel(url_queue)) {
        create_url_list(&uq->list, ngx_queue_head(&uq->list));
        return;
    }

    ngx_queue_split(&uq->list, x, &tail);
    ngx_queue_add(url_queue, &tail);

    create_url_list(&uq->list, ngx_queue_head(&uq->list));

    create_url_list(url_queue, x);
}



static url_tree_node_t *
create_urls_tree(ngx_http_request_analysis_url_ctx_t *ctx, ngx_queue_t *url_queue,size_t prefix){
    size_t                          len;
    ngx_queue_t                    *q, tail;
    url_queue_t      *uq;
    url_tree_node_t  *node;

    q = ngx_queue_middle(url_queue);

    uq = (url_queue_t *) q;
    len = uq->name->len - prefix;
    ngx_slab_pool_t *pool = ctx->url_shpool;
    node = ngx_slab_alloc(pool,offsetof(url_tree_node_t, name) + len);
    if (node == NULL) {
        return NULL;
    }

    node->left = NULL;
    node->right = NULL;
    node->tree = NULL;
    node->exact = uq->exact;
    node->len = (u_char) len;
    ngx_memcpy(node->name, &uq->name->data[prefix], len);

    node->step = uq->step;

    ngx_queue_split(url_queue, q, &tail);

    if (ngx_queue_empty(url_queue)) {
        goto inclusive;
    }

    node->left = create_urls_tree(ctx, url_queue, prefix);
    if (node->left == NULL) {
        return NULL;
    }

    ngx_queue_remove(q);

    if (ngx_queue_empty(&tail)) {
        goto inclusive;
    }

    node->right = create_urls_tree(ctx, &tail, prefix);
    if (node->right == NULL) {
        return NULL;
    }

inclusive:

    if (ngx_queue_empty(&uq->list)) {
        return node;
    }

    node->tree = create_urls_tree(ctx, &uq->list, prefix + len);
    if (node->tree == NULL) {
        return NULL;
    }

    return node;
}

static ngx_int_t
find_static_url(u_char *url,url_tree_node_t *node, ngx_str_t *path, ngx_int_t *step)
{
    u_char     *uri;
    size_t      len, n;
    ngx_int_t   rc, rv;
    len = ngx_strlen(url);
    uri = url;
    u_char *p = path->data;
    rv = NGX_DECLINED;
    for ( ;; ) {
        if (node == NULL) {
            return rv;
        }
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"test location: \"%*s\"", node->len, node->name);
        n = (len <= (size_t) node->len) ? len : node->len;
        rc = ngx_filename_cmp(uri, node->name, n);
        if (rc != 0) {
            node = (rc < 0) ? node->left : node->right;
            continue;
        }
        ngx_memcpy(p, node->name, n);
        p += n;
        path->len += n;
        *step = node->step;
        if (len > (size_t) node->len) {
            if (!node->exact) {
                rv = NGX_AGAIN;
                node = node->tree;
                uri += n;
                len -= n;
                continue;
            }
            /* exact only */
            node = node->right;
            continue;
        }

        if (len == (size_t) node->len) {
            if (node->exact) {
                return NGX_OK;
            } else {
                return NGX_AGAIN;
            }
        }
        /* len < node->len */
        node = node->left;
    }
}

static void
print_url_list_1(ngx_queue_t *pp, ngx_int_t level){
    char tmp_buf[256];
    url_queue_t *uq = (url_queue_t*)pp;
    snprintf(tmp_buf, uq->name->len + 1, "%s", uq->name->data);
    ngx_queue_t *p;
    ngx_int_t l;
    for (l = 0; l < level; l++) {
        printf("\t");
    }
    printf("%s\n", tmp_buf);
    for (p = ngx_queue_head(&uq->list); p != ngx_queue_sentinel(&uq->list); p = ngx_queue_next(p)) {
        print_url_list_1(p, ++level);
    }
}

static void
print_url_list(ngx_queue_t *qq){
    ngx_queue_t *q;
    char tmp_buf[256];
    for (q = ngx_queue_head(qq);
         q != ngx_queue_sentinel(qq);
         q = ngx_queue_next(q)) {
        url_queue_t *uq = (url_queue_t*)q;
        snprintf(tmp_buf, uq->name->len + 1, "%s", uq->name->data);

        printf("%s\n", tmp_buf);
        ngx_queue_t *p;
        for (p = ngx_queue_head(&uq->list); p != ngx_queue_sentinel(&uq->list); p = ngx_queue_next(p)) {
            print_url_list_1(p, 1);
        }
    }
}

static void
print_url_tree(url_tree_node_t *node, ngx_int_t level){

    char buf[256];
    snprintf(buf, node->len + 1, "%s", node->name);
    ngx_int_t loop;
    for (loop = 0; loop < level; loop++) {
        printf("\t");
    }

    printf("%s\n", buf);
    level++;
    if (node->left) {
        print_url_tree(node->left, level);
    }
    if (node->tree) {
        print_url_tree(node->tree, level);
    }
    if (node->right) {
        print_url_tree(node->right, level);
    }
}

static void
destroy_url_queue(ngx_http_request_analysis_url_ctx_t *ctx, ngx_queue_t *q){
    ngx_slab_pool_t *pool = ctx->url_shpool;
    url_queue_t *uq = (url_queue_t*)q;
    ngx_queue_t *p;
    url_queue_t *tp;

    for (p = ngx_queue_head(q);
         p != ngx_queue_sentinel(q);
         tp = (url_queue_t *)p,
         p = ngx_queue_next(p),
         ngx_slab_free(pool, tp->name->data),
         ngx_slab_free(pool, tp->name),
         ngx_slab_free(pool, tp)
         ) {
        for (p = ngx_queue_head(&uq->list); p != ngx_queue_sentinel(&uq->list); p = ngx_queue_next(p)) {
            destroy_url_list(ctx, p);
        }
    }
    //ngx_slab_free(pool, uq->name->data),
    //ngx_slab_free(pool, uq->name),
    ngx_slab_free(pool, uq);
    
}

static void
destroy_url_list(ngx_http_request_analysis_url_ctx_t *ctx, ngx_queue_t *q){
    ngx_slab_pool_t *pool = ctx->url_shpool;
    url_queue_t *uq = (url_queue_t*)q;
    ngx_queue_t *p;
    url_queue_t *tp;
    for (p = ngx_queue_head(&uq->list); p != ngx_queue_sentinel(&uq->list); tp = (url_queue_t *)p, p = ngx_queue_next(p),
         ngx_slab_free(pool, tp->name->data), ngx_slab_free(pool, tp->name), ngx_slab_free(pool, tp)) {
        destroy_url_list(ctx, p);
    }
    ngx_slab_free(pool, uq->name->data),
    ngx_slab_free(pool, uq->name),
    ngx_slab_free(pool, uq);
}

static void
destroy_url_tree(ngx_http_request_analysis_url_ctx_t *ctx, url_tree_node_t *node){
    ngx_slab_pool_t *pool = ctx->url_shpool;
    if (node->left) {
        destroy_url_tree(ctx, node->left);
    }
    if (node->tree) {
        destroy_url_tree(ctx, node->tree);
    }
    if (node->right) {
        destroy_url_tree(ctx, node->right);
    }
    ngx_slab_free(pool, node);
}
    
static u_char*
trim_url(u_char* url, ngx_int_t step){
    ngx_int_t len = ngx_strlen(url);
    ngx_int_t n = len;
    ngx_int_t trim = 0;
    u_char *p = url;
    while (n) {
        if (*p == '/') {
            if (trim) {
                *p = '\0';
                break;
            }
            step--;
            if (step == 0) {
                trim = 1;
            }
        }
        p++;
        n--;
    }
    return url;
}

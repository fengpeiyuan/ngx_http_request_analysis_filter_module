ngx_http_request_analysis_filter_module
====
	
																		 |ngx_http_request_analysis_url_dictionary url_dict
												|ngx_http_request_analysis_url_shctx_t *url_shm->|ngx_queue_t *urls_queue -> url_queue_t
												|						 |url_tree_node_t *urls_tree
				   |->url_shm_zone->data->ngx_http_request_analysis_url_ctx_t ->|ngx_slab_pool_t *url_shpool
				   |								|ngx_str_t url_file
				   |								
				   |														    |ngx_http_request_analysis_stat_buffer *stat_buffer
				   |								 |ngx_http_request_analysis_stat_shctx_t *stat_shm->|ngx_int_t current
				   |								 |						    |ngx_int_t last_used
				   |->stat_shm_zone->data->ngx_http_request_analysis_stat_ctx_t->|ngx_slab_pool_t *stat_shpool
				   |								 |ngx_int_t reset
ngx_http_request_analysis_conf_t ->|
				   |->stat_lock_shm_zone->data->ngx_http_request_analysis_lock_t->ngx_slab_pool_t *shpool
				   |->request_lock_shm_zone->data->ngx_http_request_analysis_lock_t->ngx_slab_pool_t *shpool

|--------url_shm-------|
|[0]|[1]|[2]| ~ |[1777]| dict
|[0]|[1]|[2]| ~~ |[100]| bucket 


memory of "ngx_http_request_analysis_stat_ctx_t->stat_shm->stat_buffer" is below

|-------process 1------||------process 2-------|| ~ ||-------process n------|
|[0]|[1]|[2]| ~~ |[100]||[0]|[1]|[2]| ~~ |[100]|| ~ ||[0]|[1]|[2]| ~~ |[100]| current
|[0]|[1]|[2]| ~~ |[100]||[0]|[1]|[2]| ~~ |[100]|| ~ ||[0]|[1]|[2]| ~~ |[100]| current_old
|[0]|[1]|[2]| ~~ |[100]||[0]|[1]|[2]| ~~ |[100]|| ~ ||[0]|[1]|[2]| ~~ |[100]| analysis data







Name
====

**ngx_http_request_analysis_filter_module**

This project is still experimental and under early development.

Description
===========

**ngx_http_request_analysis_filter_module** - one of nginx real-time requests' analysis module to show latency time. 


*This module is not distributed with the Nginx source.* See [the installation instructions](#installation).


Version
=======

This document describes ngx_http_request_analysis_filter_module released on 28 Oct 2015.

Synopsis
========

This module is a nginx filter module, which accumulate request' latency and store data in the specified shared memory zone. Each process does this individual, and collect by one of them, then analysis.
You can config bellow.
```nginx
 http {
	...
	#config in http{} or server{}
	#urlzone and stat_zone are names of url_zone and stat_zone, 50m and 200m means shared memory zone size in M of each
	#url_file is the url list to be monitored
	#reset=false means not clear data buffer when get result(location configed request_status on) every time 
	request_analysis_zone url_zone=urlzone:50m stat_zone=statzone:200m url_file=path-to/url_list reset=false;
	... 

	#get results
	location /result {
    		request_status on;
	}

```
#url list file
/a/b  #common url
/a/b 3 #/a/b,/a/b/c,/a/b/d match, /a/b/c/d,/a/b/c/d/e only match as /a/b/c
=/a/b #exact match

Limitations
===========


Installation
============

Grab the nginx source code from [nginx.org](http://nginx.org/), for example,
the version 1.7.7, and then build the source with this module:

```bash

 wget 'http://nginx.org/download/nginx-1.7.7.tar.gz'
 tar -xzvf nginx-1.7.7.tar.gz
 cd nginx-1.7.7/

 # Here we assume you would install you nginx under /opt/nginx/.
 ./configure --prefix=/opt/nginx \
     --add-module=/path/to/ngx_http_request_analysis_filter_module

 make
 make install
```

Download the latest version of the release tarball of this module

Authors
=======

* Peiyuan Feng *&lt;fengpeiyuan@gmail.com&gt;*.

This wiki page is also maintained by the author himself, and everybody is encouraged to improve this page as well.

Copyright & License
===================

Copyright (c) 2014-2015, Peiyuan Feng <fengpeiyuan@gmail.com>.

This module is licensed under the terms of the BSD license.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

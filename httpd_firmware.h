

#ifndef _HTTPD_FIRMWARE_H_
#define _HTTPD_FIRMWARE_H_

#include "esp_http_server.h"

esp_err_t index_handler(httpd_req_t *req);
esp_err_t uploadserver_handler(httpd_req_t *req);

#endif

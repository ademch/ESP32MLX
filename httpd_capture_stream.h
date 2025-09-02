#ifndef _HTTPD_CAPTURE_STREAM_H_
#define _HTTPD_CAPTURE_STREAM_H_


#include "esp_http_server.h"


esp_err_t bmp_handler(httpd_req_t *req);
esp_err_t ov2640_capture_handler(httpd_req_t *req);
esp_err_t mlx90640_capture_handler(httpd_req_t *req);
esp_err_t mlx90640_offsets_handler(httpd_req_t *req);
esp_err_t stream2640_handler(httpd_req_t *req);
esp_err_t stream90640_handler(httpd_req_t *req);

#endif
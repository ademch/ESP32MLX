
#include "httpd_firmware.h"
#include "indexHTML.h"
#include "esp_http_server.h"
#include "SPIFFS.h"
#include <time.h>

extern const char* strBuildTimestamp;

// GET /
esp_err_t index_handler(httpd_req_t *req)
{
	httpd_resp_set_hdr(req, "Cache-Control", "no-store");

	File file;
	struct tm _time;

// READ TIMESTAMP FROM SPIFFS PARTITION---------------------------------------------

	const char* pathTimestampFile = "/timestamp.txt";
	file = SPIFFS.open(pathTimestampFile, "r");

	// if file does not exist
	if (!file || !file.available()) {
		log_e("Failed to open file %s, sending index.html from build", pathTimestampFile);
		file.close();
		return httpd_resp_send(req, (const char *)index_ov2640_html_gz, sizeof(index_ov2640_html_gz) - 1);
	}

	String strSPIFFSTimestamp = file.readString();
	log_i("File content: %s", strSPIFFSTimestamp.c_str());

	file.close();

	memset(&_time, 0, sizeof(tm));
	if (strptime(strSPIFFSTimestamp.c_str(), "%a %b %d %Y %H:%M:%S", &_time) == NULL) {
		log_e("Failed to parse date string: %s", strSPIFFSTimestamp.c_str());
		return httpd_resp_send(req, (const char *)index_ov2640_html_gz, sizeof(index_ov2640_html_gz) - 1);
	}

	time_t tsSPIFFS = mktime(&_time);	// long
	log_i("SPIFFS Unix timestamp: %ld", (long)tsSPIFFS);

	// EXTRACT BUILD TIMESTAMP
	log_i("Build timestamp: %s", strBuildTimestamp);

	memset(&_time, 0, sizeof(tm));
	strptime(strBuildTimestamp, "%a %b %d %H:%M:%S %Y", &_time);

	// Convert to epoch (Unix timestamp)
	time_t tsBuild = mktime(&_time);
	log_i("Build Unix timestamp: %ld", (long)tsBuild);

	if (tsBuild > tsSPIFFS)
	{
		log_i("GET / request received, sending index.html from build");
		return httpd_resp_send(req, (const char *)index_ov2640_html_gz, sizeof(index_ov2640_html_gz) - 1);
	}

	// Open index file from SPIFFS
	const char* pathIndexFile = "/index.html";
	file = SPIFFS.open(pathIndexFile, "r");

	if (!file || !file.available()) {
		log_e("Failed to open file %s, sending index.html from build", pathIndexFile);
		file.close();
		return httpd_resp_send(req, (const char *)index_ov2640_html_gz, sizeof(index_ov2640_html_gz) - 1);
	}

	log_i("GET / request received, sending index.html from SPIFFS");

	// Stream file in chunks to client
	char buf[512];
	while (file.available()) {
		size_t len = file.readBytes(buf, sizeof(buf));
		if (httpd_resp_send_chunk(req, buf, len) != ESP_OK) {
			file.close();
			return ESP_FAIL;
		}
	}

	file.close();

	// Send zero-length chunk to indicate end
	return httpd_resp_send_chunk(req, NULL, 0);
}


// POST /uploadserver
esp_err_t uploadserver_handler(httpd_req_t *req)
{

	//- EXTRACT CLIENT DATE---------------------------------------------------

	char httpDate[128] = {};
	if (httpd_req_get_hdr_value_str(req, "X-Client-Date", httpDate, 128) != ESP_OK) {
		log_e("X-Client-Date is missing in request");
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}
	log_i("Received X-Client-Date: %s", httpDate);

//- WRITE CLIENT DATE TO FILE----------------------------------------------

	File fd;

	const char* pathTimestamp = "/timestamp.txt";
	fd = SPIFFS.open(pathTimestamp, "w");
	if (!fd) {
		log_e("Failed to open %s file for writing", pathTimestamp);
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}

	fd.print(httpDate);	// long

	fd.close();

	log_i("Timestamp written to %s", pathTimestamp);

//- RECEIVE FILE AND SAVE TO PARTITION---------------------------------------

	const char* pathIndexFile = "/index.html";
	fd = SPIFFS.open(pathIndexFile, "w");
	if (!fd) {
		log_e("Failed to open %s file for writing", pathIndexFile);
		httpd_resp_send_500(req);
		return ESP_FAIL;
	}

	int remaining = req->content_len;
	log_i("Receiving file, size: %d", remaining);

	char buf[512];
	int iRetries = 0;
	while (remaining > 0) {
		int nRead = httpd_req_recv(req, buf, sizeof(buf));
		if (nRead <= 0)
		{
			if (nRead == HTTPD_SOCK_ERR_TIMEOUT) {
				if (iRetries++ < 3) continue; // retry
			}

			log_e("Receive error");
			fd.close();
			httpd_resp_send_500(req);

			return ESP_FAIL;
		}

		fd.write((uint8_t *)buf, nRead);

		remaining -= nRead;
	}

	fd.close();

	log_i("File stored as %s", pathIndexFile);

	return httpd_resp_sendstr(req, "Upload successful");
}


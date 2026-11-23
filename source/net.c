#include "net.h"

#include <3ds.h>
#include <curl/curl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000
#define MAX_BODY (2 * 1024 * 1024)

static u32 *soc_buffer = NULL;

typedef struct {
    char *data;
    size_t len;
} Buffer;

int net_init(void)
{
    soc_buffer = (u32 *)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (!soc_buffer)
        return -1;
    if (R_FAILED(socInit(soc_buffer, SOC_BUFFERSIZE))) {
        free(soc_buffer);
        soc_buffer = NULL;
        return -1;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return 0;
}

void net_exit(void)
{
    if (!soc_buffer)
        return;
    curl_global_cleanup();
    socExit();
    free(soc_buffer);
    soc_buffer = NULL;
}

static size_t on_data(char *ptr, size_t size, size_t nmemb, void *user)
{
    Buffer *buf = (Buffer *)user;
    size_t n = size * nmemb;
    if (buf->len + n > MAX_BODY)
        return 0; /* abort: response too large */
    char *grown = realloc(buf->data, buf->len + n + 1);
    if (!grown)
        return 0;
    buf->data = grown;
    memcpy(buf->data + buf->len, ptr, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
    return n;
}

int net_get(const char *url, char **out, char *err, size_t err_size)
{
    *out = NULL;
    if (!soc_buffer) {
        snprintf(err, err_size, "Network not available");
        return -1;
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(err, err_size, "curl init failed");
        return -1;
    }

    Buffer buf = {NULL, 0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "CalTracker3DS/0.1 (Nintendo 3DS homebrew)");
    curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/cacert.pem");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 40L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        snprintf(err, err_size, "%s", curl_easy_strerror(rc));
        free(buf.data);
        return -1;
    }
    if (status != 200) {
        snprintf(err, err_size, "Server answered HTTP %ld", status);
        free(buf.data);
        return -1;
    }
    if (!buf.data) {
        snprintf(err, err_size, "Empty response");
        return -1;
    }
    *out = buf.data;
    return 0;
}

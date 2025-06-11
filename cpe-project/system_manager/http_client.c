#include "http_client.h"
#include <curl/curl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define MAX_RETRIES 3
#define RETRY_DELAY_SECONDS 1

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    return size * nmemb;
}

int send_http(const char *url, const char *json_payload) {
    CURL *curl;
    CURLcode res;
    int success = 0;
    int retries = 0;

    while (retries < MAX_RETRIES && !success) {
        curl = curl_easy_init();
        if (!curl) {
            fprintf(stderr, "ERROR: Failed to initialize curl\n");
            return 0;
        }

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");
        if (!headers) {
            fprintf(stderr, "ERROR: Failed to set headers\n");
            curl_easy_cleanup(curl);
            return 0;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(json_payload));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            // fprintf(stderr, "ERROR: curl_easy_perform() failed: %s (URL: %s, retry %d/%d)\n",
            //         curl_easy_strerror(res), url, retries + 1, MAX_RETRIES);
            if (res == CURLE_GOT_NOTHING && retries < MAX_RETRIES - 1) {
                fprintf(stderr, "Retrying in %d seconds...\n", RETRY_DELAY_SECONDS);
                sleep(RETRY_DELAY_SECONDS);
            }
        } else {
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            if (response_code >= 200 && response_code < 300) {
                success = 1;
            } else {
                fprintf(stderr, "ERROR: HTTP request failed with status %ld (URL: %s)\n", response_code, url);
            }
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        retries++;
    }

    return success;
}
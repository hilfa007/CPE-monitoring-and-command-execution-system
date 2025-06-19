// http client for sending alarm

#include "http_client.h"
#include "logger.h"
#include <curl/curl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

// maximum number of retries for HTTP requests
#define MAX_RETRIES 3
// delay between retry attempts in seconds
#define RETRY_DELAY_SECONDS 1

// handle response data from curl
// discards response data by returning the size of data received
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    return size * nmemb;
}

// send an HTTP POST request with JSON payload
// Returns 1 on success, 0 on failure
int send_http(const char *url, const char *json_payload) {
    CURL *curl;           // CURL handle for HTTP request
    CURLcode res;         // CURL result code
    int success = 0;      // Flag to track request success
    int retries = 0;      // Counter for retry attempts
    long response_code = 0; // HTTP response status code

    curl_global_init(CURL_GLOBAL_DEFAULT);

    // retry loop for handling transient failures
    while (retries < MAX_RETRIES && !success) {
        curl = curl_easy_init();
        if (!curl) {
            // Log error if CURL initialization fails
            log_message("ERROR: Failed to initialize curl for URL: %s", url);
            curl_global_cleanup();
            return 0;
        }

        // Set up HTTP headers
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");
        if (!headers) {
            // Log error if header setup fails
            log_message("ERROR: Failed to set headers for URL: %s", url);
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return 0;
        }

        // Configure CURL options for the HTTP POST request
        curl_easy_setopt(curl, CURLOPT_URL, url);                    // Set target URL
        curl_easy_setopt(curl, CURLOPT_POST, 1L);                   // Enable POST method
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);   // Set JSON payload
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(json_payload)); // Set payload size
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);        // Set HTTP headers
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback); // Set response callback
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);            // No user data for callback
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);               // Set request timeout (10 seconds)
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);        // Set connection timeout (10 seconds)
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);            // Don't fail on HTTP errors
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);                // Disable verbose output
        curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);                 // Include response body
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);          // Enable TCP keep-alive

        // HTTP request
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            // log error for all CURL failures, including CURLE_GOT_NOTHING
            log_message("ERROR: curl_easy_perform() failed: %s (URL: %s, retry %d/%d)",
                        curl_easy_strerror(res), url, retries + 1, MAX_RETRIES);
            if (retries < MAX_RETRIES - 1) {
                // log retry attempt and wait before next attempt
                log_message("INFO: Retrying in %d seconds for URL: %s", RETRY_DELAY_SECONDS, url);
                sleep(RETRY_DELAY_SECONDS);
            }
        } else {
            // get HTTP response code
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            if (response_code >= 200 && response_code < 300) {
                // log success for 2xx status codes
                success = 1;
                log_message("INFO: HTTP request succeeded with status %ld for URL: %s", response_code, url);
            } else {
                // log failure for non-2xx status codes
                log_message("ERROR: HTTP request failed with status %ld for URL: %s", response_code, url);
            }
        }

        // clean up headers and CURL handle
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        retries++;
    }

    // log final failure if all retries are exhausted
    if (!success) {
        log_message("ERROR: Failed to send HTTP request after %d retries (URL: %s, last status: %ld)",
                    MAX_RETRIES, url, response_code);
    }

    // clean up CURL global resources
    curl_global_cleanup();
    return success;
}

#include <stdio.h>
#include <string.h>
#include <curl/curl.h>

void send_alert_to_cloud(const char *message) {
    CURL *curl = curl_easy_init();
    if (curl) {
        const char *url = "http://your-cloud-endpoint.com/alert";  // Replace with your actual endpoint

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl);
    }
}

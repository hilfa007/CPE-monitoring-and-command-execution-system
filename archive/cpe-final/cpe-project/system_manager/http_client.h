// header file for http_client.c : to send alert to system manager

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

// sends an HTTP request to the specified URL with a JSON payload.
// parameters:
//   url - The destination URL for the HTTP request.
//   json_payload - The JSON-formatted string to be sent in the request body.
// returns:
//   an integer status code (implementation-defined, e.g., 0 for success, non-zero for error).
int send_http(const char *url, const char *json_payload);

#endif 

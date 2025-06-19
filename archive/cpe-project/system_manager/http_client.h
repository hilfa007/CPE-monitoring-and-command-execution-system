#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

// Sends an HTTP request to the specified URL with a JSON payload.
// Parameters:
//   url - The destination URL for the HTTP request.
//   json_payload - The JSON-formatted string to be sent in the request body.
// Returns:
//   An integer status code (implementation-defined, e.g., 0 for success, non-zero for error).
int send_http(const char *url, const char *json_payload);

#endif // HTTP_CLIENT_H

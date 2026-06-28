#ifndef NETWORK_H
#define NETWORK_H

#include "stdbool.h"
#include "stddef.h"

#define HTML_MAX_CONTENTS (1024 * 4)
#define HTML_MAX_PATH_LEN (256)
#define HTML_MAX_QUERY_LEN (256)

typedef struct http_contents
{
    char contents[HTML_MAX_CONTENTS]; //< HTML
    size_t length;                    //< HTML content length
    bool update;                      //< HTML page needs updated
} http_contents_t;

typedef struct http_request
{
    enum
    {
        GET,
        POST
    } method;
    char path[HTML_MAX_PATH_LEN];
    char query[HTML_MAX_QUERY_LEN];
} http_request_t;

int network_init(void);
int network_deinit(void);

int network_task(void);

#endif // NETWORK_H
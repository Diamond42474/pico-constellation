#include "ui/ui.h"

#include <stdio.h>
#include <string.h>
#include "c-logger.h"
#include "pages/home_page.h"
#include "interface/pconfig.h"
#include "peregrine-constellation.h"

typedef struct
{
    char name[32];
    char time[32];
    char message[100];
} message_t;

static message_t messages[32];
static int message_index = 0;
extern pc_handle_t *pc_handle;

static int _update(http_contents_t *contents, http_request_t *request);
static int _home_page(http_contents_t *contents, http_request_t *request);
static int _send(http_contents_t *contents, http_request_t *request);

int ui_init(void)
{
    return 0;
}

int ui_deinit(void)
{
    return 0;
}

int ui_handle_event(http_contents_t *contents, http_request_t *request)
{
    LOG_INFO("Path: %s", request->path);
    LOG_INFO("Query: %s", request->query);
    LOG_INFO("Body: %s", request->body);

    if (strcmp(request->path, "/update") == 0)
    {
        return _update(contents, request);
    }
    else if (strcmp(request->path, "/send") == 0)
    {
        return _send(contents, request);
    }
    else
    {
        return _home_page(contents, request);
    }

    return 0;
}

int _home_page(http_contents_t *contents, http_request_t *request)
{
    snprintf(contents->contents, HTML_MAX_CONTENTS, home_compressed, pconfigFCC_CALLSIGN);
    contents->length = strlen(contents->contents);
    contents->update = true;
}

size_t messages_to_json(char *buffer, size_t buffer_size)
{
    size_t len = 0;

    len += snprintf(buffer + len, buffer_size - len, "[");

    for (int i = 0; i < message_index; i++)
    {
        len += snprintf(
            buffer + len,
            buffer_size - len,
            "%s{\"name\":\"%s\",\"text\":\"%s\",\"time\":\"%s\"}",
            (i == 0) ? "" : ",",
            messages[i].name,
            messages[i].message,
            messages[i].time);
    }

    len += snprintf(buffer + len, buffer_size - len, "]");

    return len;
}

int _update(http_contents_t *contents, http_request_t *request)
{
    static int count = 0;
    messages_to_json(contents->contents, HTML_MAX_CONTENTS);
    contents->length = strlen(contents->contents);
    contents->update = true;

    return 0;
}

int _send(http_contents_t *contents, http_request_t *request)
{
    if (message_index < 31)
    {
        snprintf(messages[message_index].name, 32, pconfigFCC_CALLSIGN);
        snprintf(messages[message_index].time, 32, "unknown");
        snprintf(messages[message_index].message, 100, request->body);

        pc_send_message(pc_handle, 0x00, messages[message_index].message, strlen(messages[message_index].message));
        message_index++;
    }
    contents->update = false;
    contents->length = 0;
    return 0;
}
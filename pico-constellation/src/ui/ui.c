#include "ui/ui.h"

#include <stdio.h>
#include <string.h>

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
    static int count = 0;
    snprintf(contents->contents, HTML_MAX_CONTENTS, "%d\n%s\n%s\n%d", request->method, request->path, request->query, count++);
    contents->length = strlen(contents->contents);
    contents->update = true;

    return 0;
}
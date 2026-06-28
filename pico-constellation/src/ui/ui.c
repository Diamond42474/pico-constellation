#include "ui/ui.h"

#include <stdio.h>
#include <string.h>
#include "c-logger.h"

#define HTML_CONTENT                                                                \
    "<!DOCTYPE html>"                                                               \
    "<html>"                                                                        \
    "<head>"                                                                        \
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"      \
    "<style>"                                                                       \
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial;"    \
    "background:#0b0f14;color:#e6edf3;margin:0;padding:20px;}"                      \
    ".card{background:#111826;border-radius:16px;padding:16px;margin:12px 0;"       \
    "box-shadow:0 4px 16px rgba(0,0,0,0.3);}"                                       \
    ".label{color:#8b949e;font-size:13px;margin-bottom:4px;}"                       \
    ".value{font-size:18px;font-weight:600;word-break:break-word;}"                 \
    "h1{font-size:20px;margin-bottom:16px;}"                                        \
    "</style>"                                                                      \
    "</head>"                                                                       \
    "<body>"                                                                        \
    "<h1>Request Info</h1>"                                                         \
    "<div class='card'>"                                                            \
    "<div class='label'>METHOD</div><div class='value'>%d</div>"                    \
    "</div>"                                                                        \
    "<div class='card'>"                                                            \
    "<div class='label'>PATH</div><div class='value'>%s</div>"                      \
    "</div>"                                                                        \
    "<div class='card'>"                                                            \
    "<div class='label'>QUERY</div><div class='value'>%s</div>"                     \
    "</div>"                                                                        \
    "<div class='card'>"                                                            \
    "<div class='label'>COUNT</div><div class='value' id=\"count\">0</div>"         \
    "</div>"                                                                        \
    "<script>"                                                                      \
    "async function update() {"                                                     \
    "const response = await fetch(\"/count\");"                                     \
    "const data = await response.json();"\
    "document.getElementById(\"count\").textContent = data.count;" \
    "}"                                                                             \
    "setInterval(update, 1000);"                                                    \
    "update();"                                                                     \
    "</script>"                                                                     \
    "</body>"                                                                       \
    "</html>"

static int _count(http_contents_t *contents, http_request_t *request);
static int _home_page(http_contents_t *contents, http_request_t *request);

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
    if (strcmp(request->path, "/count") == 0)
    {
        return _count(contents, request);
    }
    else
    {
        return _home_page(contents, request);
    }

    return 0;
}

int _home_page(http_contents_t *contents, http_request_t *request)
{
    snprintf(contents->contents, HTML_MAX_CONTENTS, HTML_CONTENT, request->method, request->path, request->query);
    contents->length = strlen(contents->contents);
    contents->update = true;
}

int _count(http_contents_t *contents, http_request_t *request)
{
    static int count = 0;
    snprintf(contents->contents, HTML_MAX_CONTENTS, "{\"count\":%d}", count++);
    contents->length = strlen(contents->contents);
    contents->update = true;

    return 0;
}
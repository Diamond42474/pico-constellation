#ifndef UI_H
#define UI_H

#include "network/network.h"

int ui_init(void);
int ui_deinit(void);

int ui_handle_event(http_contents_t *contents, http_request_t *request);

#endif // UI_H

#ifndef COMMUNICATION_INTERFACE_H
#define COMMUNICATION_INTERFACE_H

#include <stddef.h>

int communication_interface_init(void);
int communication_interface_deinit(void);

int communication_interface_send(const void *data, size_t size);

#endif // COMMUNICATIONS_INTERFACE_H
#ifndef _DUMPER_USB_H_
#define _DUMPER_USB_H_

#include <stdint.h>
#include <stdbool.h>

void usb_init(void);
bool usb_is_configured(void);
void usb_poll(void);
void usb_write_bytes(const void *data, uint32_t len);
void usb_puts(const char *s);

#endif /* _DUMPER_USB_H_ */

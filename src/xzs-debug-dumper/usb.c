#include <stdint.h>
#include <stddef.h>
#include "usb.h"
#include "uart.h"

static void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

#define DWC3_BASE            0x06a00000UL

/* Global Registers */
#define DWC3_GSNPSID         0xc120
#define DWC3_GCTL            0xc110
#define DWC3_GEVNTADRLO(n)   (0xc400 + (n) * 0x10)
#define DWC3_GEVNTADRHI(n)   (0xc404 + (n) * 0x10)
#define DWC3_GEVNTSIZ(n)     (0xc408 + (n) * 0x10)
#define DWC3_GEVNTCNT(n)     (0xc40c + (n) * 0x10)

/* Device Registers */
#define DWC3_DCFG            0xc700
#define DWC3_DCTL            0xc704
#define DWC3_DEVTEN          0xc708
#define DWC3_DSTS            0xc70c
#define DWC3_DALEPENA        0xc720

/* Endpoint Command Registers */
#define DWC3_DEPCMDPAR2(n)   (0xc800 + (n) * 0x10)
#define DWC3_DEPCMDPAR1(n)   (0xc804 + (n) * 0x10)
#define DWC3_DEPCMDPAR0(n)   (0xc808 + (n) * 0x10)
#define DWC3_DEPCMD(n)       (0xc80c + (n) * 0x10)

/* Commands */
#define DWC3_DEPCMD_DEPCFG         (0x01 << 0)
#define DWC3_DEPCMD_DEPXFERCFG     (0x02 << 0)
#define DWC3_DEPCMD_DEPSTARTCFG    (0x09 << 0)
#define DWC3_DEPCMD_DEPSTRTXFER    (0x06 << 0)
#define DWC3_DEPCMD_CMDACT         (1 << 10)

static inline void dwc3_write32(uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)(DWC3_BASE + offset) = val;
}

static inline uint32_t dwc3_read32(uint32_t offset) {
    return *(volatile uint32_t *)(DWC3_BASE + offset);
}

/* Event buffer: 4KB aligned */
static uint32_t g_event_buf[1024] __attribute__((aligned(4096)));

/* TRBs aligned to 16 bytes */
struct dwc3_trb {
    uint32_t bpl;
    uint32_t bph;
    uint32_t size;
    uint32_t ctrl;
} __attribute__((aligned(16)));

static struct dwc3_trb ep0_out_trb __attribute__((aligned(64)));
static struct dwc3_trb ep0_in_trb  __attribute__((aligned(64)));
static struct dwc3_trb ep1_in_trb  __attribute__((aligned(64)));

static uint8_t setup_pkt[64] __attribute__((aligned(64)));
static uint8_t ep0_in_buf[512] __attribute__((aligned(64)));
static uint8_t bulk_in_buf[2048] __attribute__((aligned(64)));

static bool g_usb_configured = false;
static volatile bool ep1_in_busy = false;
static uint32_t g_event_pos = 0;

/* USB Descriptors (CDC ACM) */
static const uint8_t dev_desc[] = {
    18,         /* bLength */
    0x01,       /* bDescriptorType = Device */
    0x00, 0x02, /* bcdUSB = 2.00 */
    0x02,       /* bDeviceClass = CDC */
    0x00,       /* bDeviceSubClass */
    0x00,       /* bDeviceProtocol */
    64,         /* bMaxPacketSize0 = 64 */
    0x8a, 0x2e, /* idVendor = 0x2e8a (Raspberry Pi / Generic CDC) */
    0x0a, 0x00, /* idProduct = 0x000a */
    0x00, 0x01, /* bcdDevice = 1.00 */
    1,          /* iManufacturer */
    2,          /* iProduct */
    3,          /* iSerialNumber */
    1           /* bNumConfigurations */
};

static const uint8_t config_desc[] = {
    /* Configuration Descriptor */
    9, 0x02, 67, 0, 2, 1, 0, 0xc0, 250,

    /* Interface Association Descriptor (IAD) */
    8, 0x0b, 0, 2, 0x02, 0x02, 0x01, 0,

    /* Interface 0: CDC Control */
    9, 0x04, 0, 0, 1, 0x02, 0x02, 0x01, 0,

    /* CDC Functional Descriptors */
    5, 0x24, 0x00, 0x10, 0x01, /* Header */
    4, 0x24, 0x02, 0x02,       /* ACM (Call Management) */
    5, 0x24, 0x06, 0x00, 0x01, /* Union */
    5, 0x24, 0x01, 0x01, 0x01, /* Call Management */

    /* Endpoint 2 IN: Interrupt */
    7, 0x05, 0x82, 0x03, 16, 0, 10,

    /* Interface 1: CDC Data */
    9, 0x04, 1, 0, 2, 0x0a, 0x00, 0x00, 0,

    /* Endpoint 1 OUT: Bulk */
    7, 0x05, 0x01, 0x02, 64, 0, 0,

    /* Endpoint 1 IN: Bulk */
    7, 0x05, 0x81, 0x02, 64, 0, 0
};

static const uint8_t str0_desc[] = { 4, 0x03, 0x09, 0x04 }; /* English */

static const uint8_t str_mfg[] = {
    10, 0x03, 'S',0, 'o',0, 'n',0, 'y',0
};

static const uint8_t str_prod[] = {
    46, 0x03,
    'X',0, 'p',0, 'e',0, 'r',0, 'i',0, 'a',0, ' ',0,
    'X',0, 'Z',0, 's',0, ' ',0, 'D',0, 'u',0, 'm',0,
    'p',0, 'e',0, 'r',0, ' ',0, 'U',0, 'S',0, 'B',0
};

static const uint8_t str_sn[] = {
    22, 0x03,
    'B',0, 'H',0, '9',0, '0',0, '5',0, 'S',0, 'X',0, '9',0, '7',0, '6',0
};

static int dwc3_ep_cmd(uint32_t ep, uint32_t cmd, uint32_t p0, uint32_t p1, uint32_t p2) {
    dwc3_write32(DWC3_DEPCMDPAR2(ep), p2);
    dwc3_write32(DWC3_DEPCMDPAR1(ep), p1);
    dwc3_write32(DWC3_DEPCMDPAR0(ep), p0);
    dwc3_write32(DWC3_DEPCMD(ep), cmd | DWC3_DEPCMD_CMDACT);
    uint32_t timeout = 50000;
    while ((dwc3_read32(DWC3_DEPCMD(ep)) & DWC3_DEPCMD_CMDACT) && --timeout);
    return (timeout == 0) ? -1 : 0;
}

static void dwc3_queue_setup(void) {
    ep0_out_trb.bpl = (uint32_t)(uintptr_t)setup_pkt;
    ep0_out_trb.bph = 0;
    ep0_out_trb.size = 8;
    /* TRBCTL = 2 (CONTROL_SETUP), HWO = 1, IOC = 1, ISP = 1 */
    ep0_out_trb.ctrl = (2 << 4) | (1 << 0) | (1 << 10) | (1 << 11);

    dwc3_ep_cmd(0, DWC3_DEPCMD_DEPSTRTXFER, (uint32_t)(uintptr_t)&ep0_out_trb, 0, 0);
}

static void dwc3_send_ep0(const void *data, uint32_t len) {
    if (len > sizeof(ep0_in_buf)) len = sizeof(ep0_in_buf);
    memcpy(ep0_in_buf, data, len);

    ep0_in_trb.bpl = (uint32_t)(uintptr_t)ep0_in_buf;
    ep0_in_trb.bph = 0;
    ep0_in_trb.size = len;
    /* TRBCTL = 3 (CONTROL_DATA), HWO = 1, LST = 1, IOC = 1 */
    ep0_in_trb.ctrl = (3 << 4) | (1 << 0) | (1 << 1) | (1 << 10);

    dwc3_ep_cmd(1, DWC3_DEPCMD_DEPSTRTXFER, (uint32_t)(uintptr_t)&ep0_in_trb, 0, 0);

    /* Queue 0-byte Status OUT on Physical EP 0 */
    ep0_out_trb.bpl = (uint32_t)(uintptr_t)setup_pkt;
    ep0_out_trb.bph = 0;
    ep0_out_trb.size = 0;
    /* TRBCTL = 4 (CONTROL_STATUS2/3), HWO = 1, LST = 1, IOC = 1 */
    ep0_out_trb.ctrl = (4 << 4) | (1 << 0) | (1 << 1) | (1 << 10);
    dwc3_ep_cmd(0, DWC3_DEPCMD_DEPSTRTXFER, (uint32_t)(uintptr_t)&ep0_out_trb, 0, 0);
}

static void dwc3_status_in(void) {
    /* Send 0-byte Status IN on Physical EP 1 */
    ep0_in_trb.bpl = (uint32_t)(uintptr_t)ep0_in_buf;
    ep0_in_trb.bph = 0;
    ep0_in_trb.size = 0;
    /* TRBCTL = 4 (CONTROL_STATUS2/3), HWO = 1, LST = 1, IOC = 1 */
    ep0_in_trb.ctrl = (4 << 4) | (1 << 0) | (1 << 1) | (1 << 10);
    dwc3_ep_cmd(1, DWC3_DEPCMD_DEPSTRTXFER, (uint32_t)(uintptr_t)&ep0_in_trb, 0, 0);
}

static void dwc3_setup_ep0(void) {
    dwc3_ep_cmd(0, DWC3_DEPCMD_DEPSTARTCFG, 0, 0, 0);

    /* EP0-OUT (Physical EP 0) */
    dwc3_ep_cmd(0, DWC3_DEPCMD_DEPCFG, (64 << 3) | (0 << 1), (0 << 24), 0);

    /* EP0-IN (Physical EP 1) */
    dwc3_ep_cmd(1, DWC3_DEPCMD_DEPCFG, (64 << 3) | (0 << 1), (0x80 << 24), 0);

    dwc3_ep_cmd(0, DWC3_DEPCMD_DEPXFERCFG, 1, 0, 0);
    dwc3_ep_cmd(1, DWC3_DEPCMD_DEPXFERCFG, 1, 0, 0);
}

void usb_init(void) {
    uint32_t snpsid = dwc3_read32(DWC3_GSNPSID);
    uart_puts("[DUMPER-USB] DWC3 Core ID: 0x");
    uart_puthex64(snpsid);
    uart_puts("\n");

    /* Disconnect pull-up cleanly */
    dwc3_write32(DWC3_DCTL, dwc3_read32(DWC3_DCTL) & ~(1 << 31));

    /* Configure High-Speed device mode */
    uint32_t dcfg = dwc3_read32(DWC3_DCFG);
    dcfg &= ~0x7; /* DevSpd = High Speed (0) */
    dwc3_write32(DWC3_DCFG, dcfg);

    /* Set up Event Buffer 0 */
    dwc3_write32(DWC3_GEVNTADRLO(0), (uint32_t)(uintptr_t)g_event_buf);
    dwc3_write32(DWC3_GEVNTADRHI(0), 0);
    dwc3_write32(DWC3_GEVNTSIZ(0), sizeof(g_event_buf));
    dwc3_write32(DWC3_GEVNTCNT(0), 0);
    g_event_pos = 0;

    /* Enable device events */
    dwc3_write32(DWC3_DEVTEN, (1 << 0) | (1 << 1) | (1 << 3));

    /* Initialize control endpoints */
    dwc3_setup_ep0();

    /* Enable Physical EP 0 and 1 */
    dwc3_write32(DWC3_DALEPENA, (1 << 0) | (1 << 1));

    /* Queue initial SETUP packet */
    dwc3_queue_setup();

    /* Re-attach pull-up (RunStop = 1) */
    dwc3_write32(DWC3_DCTL, dwc3_read32(DWC3_DCTL) | (1 << 31));

    uart_puts("[DUMPER-USB] Attached to USB bus in CDC ACM mode\n");
}

bool usb_is_configured(void) {
    return g_usb_configured;
}

static void handle_setup_packet(const uint8_t *pkt) {
    uint8_t req_type = pkt[0];
    uint8_t req = pkt[1];
    uint16_t val = (pkt[3] << 8) | pkt[2];
    uint16_t len = (pkt[7] << 8) | pkt[6];

    if ((req_type & 0x60) == 0x00) { /* Standard Request */
        switch (req) {
            case 0x06: /* GET_DESCRIPTOR */
                switch (pkt[3]) {
                    case 0x01: /* DEVICE */
                        dwc3_send_ep0(dev_desc, len < sizeof(dev_desc) ? len : sizeof(dev_desc));
                        return;
                    case 0x02: /* CONFIGURATION */
                        dwc3_send_ep0(config_desc, len < sizeof(config_desc) ? len : sizeof(config_desc));
                        return;
                    case 0x03: /* STRING */
                        switch (pkt[2]) {
                            case 0: dwc3_send_ep0(str0_desc, len < sizeof(str0_desc) ? len : sizeof(str0_desc)); return;
                            case 1: dwc3_send_ep0(str_mfg, len < sizeof(str_mfg) ? len : sizeof(str_mfg)); return;
                            case 2: dwc3_send_ep0(str_prod, len < sizeof(str_prod) ? len : sizeof(str_prod)); return;
                            case 3: dwc3_send_ep0(str_sn, len < sizeof(str_sn) ? len : sizeof(str_sn)); return;
                            default: break;
                        }
                        break;
                    default: break;
                }
                break;

            case 0x05: /* SET_ADDRESS */
                {
                    uint32_t dcfg = dwc3_read32(DWC3_DCFG);
                    dcfg &= ~(0x7f << 3);
                    dcfg |= ((val & 0x7f) << 3);
                    dwc3_write32(DWC3_DCFG, dcfg);
                    dwc3_status_in();
                    dwc3_queue_setup();
                    return;
                }

            case 0x09: /* SET_CONFIGURATION */
                /* Physical EP 3: Bulk IN, MPS = 64 */
                dwc3_ep_cmd(3, DWC3_DEPCMD_DEPCFG, (64 << 3) | (2 << 1), (0x81 << 24), 0);
                dwc3_ep_cmd(3, DWC3_DEPCMD_DEPXFERCFG, 1, 0, 0);

                /* Enable EP0-OUT(0), EP0-IN(1), EP1-IN(3) */
                dwc3_write32(DWC3_DALEPENA, (1 << 0) | (1 << 1) | (1 << 3));
                g_usb_configured = true;
                uart_puts("[DUMPER-USB] Interface CONFIGURED by Host!\n");
                dwc3_status_in();
                dwc3_queue_setup();
                return;

            default:
                break;
        }
    } else if ((req_type & 0x60) == 0x20) { /* Class CDC Request */
        dwc3_status_in();
        dwc3_queue_setup();
        return;
    }

    dwc3_status_in();
    dwc3_queue_setup();
}

void usb_poll(void) {
    uint32_t count = dwc3_read32(DWC3_GEVNTCNT(0));
    if (count == 0) return;

    while (count >= 4) {
        uint32_t event = g_event_buf[g_event_pos];
        g_event_pos = (g_event_pos + 1) % 1024;
        count -= 4;
        dwc3_write32(DWC3_GEVNTCNT(0), 4);

        if (event & 1) {
            /* Endpoint event */
            uint32_t ep_num = (event >> 1) & 0x1f;
            uint32_t ep_event = (event >> 6) & 0x0f;

            if (ep_num == 0 && ep_event == 1) {
                /* XferComplete on EP0-OUT (Setup packet received) */
                handle_setup_packet(setup_pkt);
            } else if (ep_num == 3) {
                /* XferComplete on EP1-IN (Bulk data sent) */
                ep1_in_busy = false;
            }
        } else {
            /* Device event */
            uint32_t dev_event = (event >> 8) & 0x0f;
            if (dev_event == 1) {
                /* Bus Reset */
                g_usb_configured = false;
                ep1_in_busy = false;
                dwc3_setup_ep0();
                dwc3_write32(DWC3_DALEPENA, (1 << 0) | (1 << 1));
                dwc3_queue_setup();
            } else if (dev_event == 3) {
                /* Connection Done */
                g_usb_configured = false;
                ep1_in_busy = false;
                uart_puts("[DUMPER-USB] Connection Done (High Speed)\n");
            }
        }
    }
}

void usb_write_bytes(const void *data, uint32_t len) {
    if (!g_usb_configured || len == 0 || !data) return;

    const uint8_t *ptr = (const uint8_t *)data;
    while (len > 0) {
        uint32_t chunk = len < sizeof(bulk_in_buf) ? len : sizeof(bulk_in_buf);

        /* Wait for previous EP1-IN transfer to complete */
        uint32_t timeout = 50000;
        while (ep1_in_busy && timeout > 0) {
            usb_poll();
            timeout--;
        }
        if (ep1_in_busy) {
            ep1_in_busy = false;
        }

        memcpy(bulk_in_buf, ptr, chunk);

        ep1_in_trb.bpl = (uint32_t)(uintptr_t)bulk_in_buf;
        ep1_in_trb.bph = 0;
        ep1_in_trb.size = chunk;
        /* TRBCTL = 1 (NORMAL), HWO = 1, LST = 1, IOC = 1 */
        ep1_in_trb.ctrl = (1 << 4) | (1 << 0) | (1 << 1) | (1 << 10);

        ep1_in_busy = true;
        dwc3_ep_cmd(3, DWC3_DEPCMD_DEPSTRTXFER, (uint32_t)(uintptr_t)&ep1_in_trb, 0, 0);

        ptr += chunk;
        len -= chunk;
    }
}

void usb_puts(const char *s) {
    if (s) {
        usb_write_bytes(s, (uint32_t)strlen(s));
    }
}

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#define XZS_UART_RX_BUF_SIZE 256
#define XZS_UART_RX_BUF_MASK (XZS_UART_RX_BUF_SIZE - 1)

struct xzs_uart_rx_buf {
    uint8_t data[XZS_UART_RX_BUF_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overflow_count;
};

static inline void xzs_rx_buf_init(struct xzs_uart_rx_buf *buf)
{
    memset((void *)buf->data, 0, sizeof(buf->data));
    buf->head = 0;
    buf->tail = 0;
    buf->overflow_count = 0;
}

static inline bool xzs_rx_buf_is_empty(const struct xzs_uart_rx_buf *buf)
{
    return buf->head == buf->tail;
}

static inline bool xzs_rx_buf_is_full(const struct xzs_uart_rx_buf *buf)
{
    return ((buf->head + 1) & XZS_UART_RX_BUF_MASK) == buf->tail;
}

static inline uint32_t xzs_rx_buf_count(const struct xzs_uart_rx_buf *buf)
{
    return (buf->head - buf->tail) & XZS_UART_RX_BUF_MASK;
}

static inline bool xzs_rx_buf_put(struct xzs_uart_rx_buf *buf, uint8_t byte)
{
    uint32_t next_head = (buf->head + 1) & XZS_UART_RX_BUF_MASK;
    if (next_head == buf->tail) {
        buf->overflow_count++;
        return false; /* Buffer full, drop byte */
    }
    buf->data[buf->head] = byte;
    __asm__ volatile("" ::: "memory"); /* Memory barrier */
    buf->head = next_head;
    return true;
}

static inline int xzs_rx_buf_get(struct xzs_uart_rx_buf *buf)
{
    if (buf->head == buf->tail) {
        return -1; /* Empty */
    }
    uint8_t byte = buf->data[buf->tail];
    __asm__ volatile("" ::: "memory"); /* Memory barrier */
    buf->tail = (buf->tail + 1) & XZS_UART_RX_BUF_MASK;
    return (int)byte;
}

int main(void)
{
    struct xzs_uart_rx_buf buf;
    printf("[*] Running XZS UART RX Ring Buffer Unit Tests...\n");

    /* Test 1: Empty on init */
    xzs_rx_buf_init(&buf);
    assert(xzs_rx_buf_is_empty(&buf));
    assert(!xzs_rx_buf_is_full(&buf));
    assert(xzs_rx_buf_count(&buf) == 0);
    assert(xzs_rx_buf_get(&buf) == -1);
    printf("  [PASS] Test 1: Empty state initialized\n");

    /* Test 2: Single byte enqueue and dequeue */
    assert(xzs_rx_buf_put(&buf, 0x41));
    assert(!xzs_rx_buf_is_empty(&buf));
    assert(xzs_rx_buf_count(&buf) == 1);
    int c = xzs_rx_buf_get(&buf);
    assert(c == 0x41);
    assert(xzs_rx_buf_is_empty(&buf));
    assert(xzs_rx_buf_count(&buf) == 0);
    printf("  [PASS] Test 2: Single byte enqueue and dequeue\n");

    /* Test 3: Multiple bytes FIFO ordering */
    const char *test_str = "abc\n";
    for (size_t i = 0; i < strlen(test_str); i++) {
        assert(xzs_rx_buf_put(&buf, (uint8_t)test_str[i]));
    }
    assert(xzs_rx_buf_count(&buf) == strlen(test_str));
    for (size_t i = 0; i < strlen(test_str); i++) {
        c = xzs_rx_buf_get(&buf);
        assert(c == (int)test_str[i]);
    }
    assert(xzs_rx_buf_is_empty(&buf));
    printf("  [PASS] Test 3: Multiple bytes FIFO ordering preserved\n");

    /* Test 4: Wraparound across buffer boundary */
    buf.head = XZS_UART_RX_BUF_SIZE - 2;
    buf.tail = XZS_UART_RX_BUF_SIZE - 2;
    for (int i = 0; i < 10; i++) {
        assert(xzs_rx_buf_put(&buf, (uint8_t)(0x30 + i)));
    }
    assert(xzs_rx_buf_count(&buf) == 10);
    for (int i = 0; i < 10; i++) {
        c = xzs_rx_buf_get(&buf);
        assert(c == 0x30 + i);
    }
    assert(xzs_rx_buf_is_empty(&buf));
    printf("  [PASS] Test 4: Wraparound across 256-byte boundary\n");

    /* Test 5: Full condition and overflow policy */
    xzs_rx_buf_init(&buf);
    for (int i = 0; i < XZS_UART_RX_BUF_SIZE - 1; i++) {
        assert(xzs_rx_buf_put(&buf, (uint8_t)(i & 0xFF)));
    }
    assert(xzs_rx_buf_is_full(&buf));
    assert(xzs_rx_buf_count(&buf) == XZS_UART_RX_BUF_SIZE - 1);
    /* Next put must fail and increment overflow_count */
    assert(!xzs_rx_buf_put(&buf, 0xFF));
    assert(buf.overflow_count == 1);
    /* Ensure existing data is intact */
    for (int i = 0; i < XZS_UART_RX_BUF_SIZE - 1; i++) {
        c = xzs_rx_buf_get(&buf);
        assert(c == (i & 0xFF));
    }
    assert(xzs_rx_buf_is_empty(&buf));
    printf("  [PASS] Test 5: Full condition and overflow drop policy verified\n");

    printf("[+] ALL 5 RX RING BUFFER UNIT TESTS PASSED!\n");
    return 0;
}

#ifndef _OS_FIREHOSE_BUFFER_PRIVATE_H_
#define _OS_FIREHOSE_BUFFER_PRIVATE_H_

#include <stdint.h>
#include <stdbool.h>
#include <firehose/tracepoint_private.h>
#include <firehose/chunk_private.h>
#include <firehose/firehose_types_private.h>

#define FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT 64
#define FIREHOSE_BUFFER_KERNEL_DEFAULT_CHUNK_COUNT 64
#define FIREHOSE_BUFFER_KERNEL_DEFAULT_IO_PAGES 2

struct firehose_buffer_range_s {
    uint16_t fbr_offset;
    uint16_t fbr_length;
};

static inline bool __firehose_kernel_configuration_valid(uint8_t chunks, uint8_t io_pages) {
    return (chunks > 0 && io_pages > 0);
}

static inline void *__firehose_buffer_create(size_t *size) {
    if (size) {
        *size = 0;
    }
    return (void *)0;
}

static inline bool __firehose_merge_updates(firehose_push_reply_t reply) {
    (void)reply;
    return false;
}

static inline firehose_tracepoint_t
__firehose_buffer_tracepoint_reserve(uint64_t stamp, firehose_stream_t stream,
    uint16_t pubsize, uint16_t privsize, uint8_t **privptr)
{
    (void)stamp;
    (void)stream;
    (void)pubsize;
    (void)privsize;
    (void)privptr;
    return (firehose_tracepoint_t)0;
}

static inline void
__firehose_buffer_tracepoint_flush(firehose_tracepoint_t vat,
    firehose_tracepoint_id_u vatid)
{
    (void)vat;
    (void)vatid;
}

#endif /* _OS_FIREHOSE_BUFFER_PRIVATE_H_ */

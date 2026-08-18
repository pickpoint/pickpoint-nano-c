#ifndef PICKPOINT_NANO_H
#define PICKPOINT_NANO_H

/*
 * Tiny tracking.v2 HTTP ingest helper for GPS beacons.
 * Not a mini Pickpoint SDK: no geocoding, routing, listener, WebSocket,
 * GPS filter, or HTTP client. Firmware (usually a modem) performs HTTPS.
 *
 * C99. Link as libpickpoint_nano. Include from C or C++ (extern "C").
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PICKPOINT_NANO_VERSION "2.0.0"

#define PICKPOINT_NANO_DEFAULT_INGEST_URL \
  "https://tracking.pickpoint.io/v2/ingest"
#define PICKPOINT_NANO_DEFAULT_QUEUE 256u
#define PICKPOINT_NANO_MAX_QUEUE 1024u
#define PICKPOINT_NANO_MAX_BODY 8192u
#define PICKPOINT_NANO_MAX_FRAMES 8u
#define PICKPOINT_NANO_MAX_LOC_POINTS 100u

#define PICKPOINT_NANO_OK 0
#define PICKPOINT_NANO_ERR_ARG (-1)
#define PICKPOINT_NANO_ERR_COORD (-2)
#define PICKPOINT_NANO_ERR_QUEUE_CAP (-3)
#define PICKPOINT_NANO_ERR_DECODE (-4)
#define PICKPOINT_NANO_ERR_NOMEM (-5)
#define PICKPOINT_NANO_ERR_NOSPACE (-6)

typedef struct pickpoint_nano pickpoint_nano;

typedef struct pickpoint_nano_flush_result {
  uint64_t last_acked;
  char track_uid[37];
  char relocate_endpoint[256];
  uint32_t retry_after_ms;
  int error_code; /* wire Error.code, 0 if none */
  char error_message[256];
  int stopped;
} pickpoint_nano_flush_result;

pickpoint_nano *pickpoint_nano_new(const char *client_id,
                                   const char *client_secret);
void pickpoint_nano_free(pickpoint_nano *n);

int pickpoint_nano_set_ingest_url(pickpoint_nano *n, const char *url);
int pickpoint_nano_set_queue_cap(pickpoint_nano *n, size_t cap);

size_t pickpoint_nano_queue_len(const pickpoint_nano *n);
uint64_t pickpoint_nano_last_acked(const pickpoint_nano *n);
const char *pickpoint_nano_track_uid(const pickpoint_nano *n);

/* timestamp_ms == 0 omits the time flag on the wire. */
int pickpoint_nano_push(pickpoint_nano *n, double lat_deg, double lon_deg,
                        int64_t timestamp_ms);
void pickpoint_nano_request_stop(pickpoint_nano *n);

/*
 * Build one POST. Returns 1 and fills url/body, 0 if nothing to send,
 * negative on error. Auth is always in the query string.
 */
int pickpoint_nano_flush_request(pickpoint_nano *n, char *url, size_t url_cap,
                                 uint8_t *body, size_t body_cap,
                                 size_t *body_len);

int pickpoint_nano_handle_response(pickpoint_nano *n, const uint8_t *body,
                                   size_t len,
                                   pickpoint_nano_flush_result *out);

#ifdef __cplusplus
}
#endif

#endif /* PICKPOINT_NANO_H */

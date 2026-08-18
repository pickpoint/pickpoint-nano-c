#include "pickpoint_nano.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C_TRACK_START 0x02u
#define C_TRACK_STOP 0x03u
#define C_LOC 0x04u

#define S_RELOCATE 0x81u
#define S_TRACK_STARTED 0x83u
#define S_TRACK_STOPPED 0x84u
#define S_ACK 0x85u
#define S_ERROR 0x88u

#define PF_TIME (1u << 4)
#define LAT_MIN (-90000000)
#define LAT_MAX 90000000
#define LON_MIN (-180000000)
#define LON_MAX 180000000
#define MAX_STRING 4096u
#define MAX_URL 1024u
#define MAX_ID 256u

typedef struct {
  uint64_t seq;
  int32_t lat_micro;
  int32_t lon_micro;
  int64_t timestamp_ms;
} queued_point;

struct pickpoint_nano {
  char client_id[MAX_ID];
  char client_secret[MAX_ID];
  char ingest_url[MAX_URL];
  char track_uid[37];
  size_t cap;
  size_t qlen;
  queued_point *queue;
  uint64_t next_seq;
  uint64_t last_acked;
  int started;
  int stop_pending;
};

static int32_t deg_to_micro(double d) {
  double x = d * 1000000.0;
  if (x >= 0.0) {
    return (int32_t)(x + 0.5);
  }
  return (int32_t)(x - 0.5);
}

static int check_coord(int32_t lat, int32_t lon) {
  return lat >= LAT_MIN && lat <= LAT_MAX && lon >= LON_MIN && lon <= LON_MAX;
}

static int copy_str(char *dst, size_t cap, const char *src) {
  size_t n;
  if (!src) {
    return PICKPOINT_NANO_ERR_ARG;
  }
  n = strlen(src);
  if (n + 1 > cap) {
    return PICKPOINT_NANO_ERR_NOSPACE;
  }
  memcpy(dst, src, n + 1);
  return PICKPOINT_NANO_OK;
}

static void enforce_cap(pickpoint_nano *n) {
  if (n->qlen <= n->cap) {
    return;
  }
  {
    size_t drop = n->qlen - n->cap;
    memmove(n->queue, n->queue + drop, (n->qlen - drop) * sizeof(queued_point));
    n->qlen -= drop;
  }
}

pickpoint_nano *pickpoint_nano_new(const char *client_id,
                                   const char *client_secret) {
  pickpoint_nano *n;
  if (!client_id || !client_secret || client_id[0] == '\0' ||
      client_secret[0] == '\0') {
    return NULL;
  }
  n = (pickpoint_nano *)calloc(1, sizeof(*n));
  if (!n) {
    return NULL;
  }
  if (copy_str(n->client_id, sizeof(n->client_id), client_id) != 0 ||
      copy_str(n->client_secret, sizeof(n->client_secret), client_secret) !=
          0) {
    free(n);
    return NULL;
  }
  memcpy(n->ingest_url, PICKPOINT_NANO_DEFAULT_INGEST_URL,
         sizeof(PICKPOINT_NANO_DEFAULT_INGEST_URL));
  n->cap = PICKPOINT_NANO_DEFAULT_QUEUE;
  n->queue = (queued_point *)calloc(PICKPOINT_NANO_MAX_QUEUE, sizeof(queued_point));
  if (!n->queue) {
    free(n);
    return NULL;
  }
  return n;
}

void pickpoint_nano_free(pickpoint_nano *n) {
  if (!n) {
    return;
  }
  free(n->queue);
  free(n);
}

int pickpoint_nano_set_ingest_url(pickpoint_nano *n, const char *url) {
  if (!n) {
    return PICKPOINT_NANO_ERR_ARG;
  }
  return copy_str(n->ingest_url, sizeof(n->ingest_url), url);
}

int pickpoint_nano_set_queue_cap(pickpoint_nano *n, size_t cap) {
  if (!n) {
    return PICKPOINT_NANO_ERR_ARG;
  }
  if (cap == 0 || cap > PICKPOINT_NANO_MAX_QUEUE) {
    return PICKPOINT_NANO_ERR_QUEUE_CAP;
  }
  n->cap = cap;
  enforce_cap(n);
  return PICKPOINT_NANO_OK;
}

size_t pickpoint_nano_queue_len(const pickpoint_nano *n) {
  return n ? n->qlen : 0;
}

uint64_t pickpoint_nano_last_acked(const pickpoint_nano *n) {
  return n ? n->last_acked : 0;
}

const char *pickpoint_nano_track_uid(const pickpoint_nano *n) {
  if (!n || n->track_uid[0] == '\0') {
    return NULL;
  }
  return n->track_uid;
}

int pickpoint_nano_push(pickpoint_nano *n, double lat_deg, double lon_deg,
                        int64_t timestamp_ms) {
  int32_t lat;
  int32_t lon;
  queued_point *q;
  if (!n) {
    return PICKPOINT_NANO_ERR_ARG;
  }
  lat = deg_to_micro(lat_deg);
  lon = deg_to_micro(lon_deg);
  if (!check_coord(lat, lon)) {
    return PICKPOINT_NANO_ERR_COORD;
  }
  if (n->next_seq == UINT64_MAX) {
    return PICKPOINT_NANO_ERR_ARG;
  }
  n->next_seq += 1;
  q = &n->queue[n->qlen];
  /* If already at MAX_QUEUE, drop oldest first so we always have a slot. */
  if (n->qlen >= PICKPOINT_NANO_MAX_QUEUE) {
    memmove(n->queue, n->queue + 1, (n->qlen - 1) * sizeof(queued_point));
    n->qlen -= 1;
    q = &n->queue[n->qlen];
  }
  q->seq = n->next_seq;
  q->lat_micro = lat;
  q->lon_micro = lon;
  q->timestamp_ms = timestamp_ms;
  n->qlen += 1;
  enforce_cap(n);
  return PICKPOINT_NANO_OK;
}

void pickpoint_nano_request_stop(pickpoint_nano *n) {
  if (n) {
    n->stop_pending = 1;
  }
}

static int query_encode(const char *s, char *out, size_t cap) {
  static const char hex[] = "0123456789ABCDEF";
  size_t o = 0;
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    unsigned char c = *p++;
    int unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                     c == '.' || c == '~';
    if (unreserved) {
      if (o + 1 >= cap) {
        return -1;
      }
      out[o++] = (char)c;
    } else {
      if (o + 3 >= cap) {
        return -1;
      }
      out[o++] = '%';
      out[o++] = hex[c >> 4];
      out[o++] = hex[c & 0x0f];
    }
  }
  if (o >= cap) {
    return -1;
  }
  out[o] = '\0';
  return 0;
}

static int build_url(const pickpoint_nano *n, char *url, size_t url_cap) {
  char id_enc[MAX_ID * 3];
  char sec_enc[MAX_ID * 3];
  char sep;
  int nw;
  if (query_encode(n->client_id, id_enc, sizeof(id_enc)) != 0 ||
      query_encode(n->client_secret, sec_enc, sizeof(sec_enc)) != 0) {
    return PICKPOINT_NANO_ERR_NOSPACE;
  }
  sep = strchr(n->ingest_url, '?') ? '&' : '?';
  nw = snprintf(url, url_cap, "%s%cclient-id=%s&client-secret=%s",
                n->ingest_url, sep, id_enc, sec_enc);
  if (nw < 0 || (size_t)nw >= url_cap) {
    return PICKPOINT_NANO_ERR_NOSPACE;
  }
  return PICKPOINT_NANO_OK;
}

static int micro_delta_fits(int32_t plat, int32_t plon, int32_t lat,
                            int32_t lon) {
  int64_t dlat = (int64_t)lat - (int64_t)plat;
  int64_t dlon = (int64_t)lon - (int64_t)plon;
  return dlat >= INT16_MIN && dlat <= INT16_MAX && dlon >= INT16_MIN &&
         dlon <= INT16_MAX;
}

static void put_u8(uint8_t *b, size_t *n, uint8_t v) { b[(*n)++] = v; }
static void put_u16le(uint8_t *b, size_t *n, uint16_t v) {
  b[(*n)++] = (uint8_t)(v & 0xff);
  b[(*n)++] = (uint8_t)((v >> 8) & 0xff);
}
static void put_u32le(uint8_t *b, size_t *n, uint32_t v) {
  b[(*n)++] = (uint8_t)(v & 0xff);
  b[(*n)++] = (uint8_t)((v >> 8) & 0xff);
  b[(*n)++] = (uint8_t)((v >> 16) & 0xff);
  b[(*n)++] = (uint8_t)((v >> 24) & 0xff);
}
static void put_i16le(uint8_t *b, size_t *n, int16_t v) {
  put_u16le(b, n, (uint16_t)v);
}
static void put_i32le(uint8_t *b, size_t *n, int32_t v) {
  put_u32le(b, n, (uint32_t)v);
}
static void put_i64le(uint8_t *b, size_t *n, int64_t v) {
  uint32_t lo = (uint32_t)v;
  uint32_t hi = (uint32_t)((uint64_t)v >> 32);
  put_u32le(b, n, lo);
  put_u32le(b, n, hi);
}

static void write_point(uint8_t *b, size_t *n, const queued_point *p, int has_prev,
                        int32_t plat, int32_t plon) {
  uint8_t flags = 0;
  if (p->timestamp_ms != 0) {
    flags |= PF_TIME;
  }
  put_u8(b, n, flags);
  if (has_prev) {
    put_i16le(b, n, (int16_t)(p->lat_micro - plat));
    put_i16le(b, n, (int16_t)(p->lon_micro - plon));
  } else {
    put_i32le(b, n, p->lat_micro);
    put_i32le(b, n, p->lon_micro);
  }
  if (p->timestamp_ms != 0) {
    put_i64le(b, n, p->timestamp_ms);
  }
}

static int append_bytes(uint8_t *body, size_t *len, size_t cap, const uint8_t *src,
                        size_t n) {
  if (*len + n > cap) {
    return -1;
  }
  memcpy(body + *len, src, n);
  *len += n;
  return 0;
}

int pickpoint_nano_flush_request(pickpoint_nano *n, char *url, size_t url_cap,
                                 uint8_t *body, size_t body_cap,
                                 size_t *body_len) {
  size_t frames = 0;
  size_t len = 0;
  int rc;
  if (!n || !url || !body || !body_len || url_cap == 0 || body_cap == 0) {
    return PICKPOINT_NANO_ERR_ARG;
  }
  *body_len = 0;
  if (n->qlen == 0 && !n->stop_pending) {
    return 0;
  }
  rc = build_url(n, url, url_cap);
  if (rc != PICKPOINT_NANO_OK) {
    return rc;
  }

  if (!n->started && n->qlen > 0) {
    uint8_t start[7];
    size_t s = 0;
    put_u8(start, &s, C_TRACK_START);
    put_u8(start, &s, 0);
    put_u16le(start, &s, 0);
    put_u16le(start, &s, 0);
    if (append_bytes(body, &len, body_cap, start, s) != 0) {
      return PICKPOINT_NANO_ERR_NOSPACE;
    }
    frames += 1;
  }

  if (n->qlen > 0) {
    size_t take = n->qlen;
    size_t i = 0;
    uint64_t first_seq;
    if (take > PICKPOINT_NANO_MAX_LOC_POINTS) {
      take = PICKPOINT_NANO_MAX_LOC_POINTS;
    }
    first_seq = n->queue[take - 1].seq + 1 - (uint64_t)take;
    while (i < take && frames < PICKPOINT_NANO_MAX_FRAMES) {
      size_t start = i;
      size_t j;
      uint8_t frame[2048];
      size_t fn = 0;
      uint64_t seq;
      int32_t plat = n->queue[i].lat_micro;
      int32_t plon = n->queue[i].lon_micro;
      i += 1;
      while (i < take && (i - start) < PICKPOINT_NANO_MAX_LOC_POINTS) {
        queued_point *p = &n->queue[i];
        if (!micro_delta_fits(plat, plon, p->lat_micro, p->lon_micro)) {
          break;
        }
        plat = p->lat_micro;
        plon = p->lon_micro;
        i += 1;
      }
      seq = first_seq + (uint64_t)i - 1;
      put_u8(frame, &fn, C_LOC);
      put_u32le(frame, &fn, (uint32_t)seq);
      put_u8(frame, &fn, (uint8_t)(i - start));
      {
        int has_prev = 0;
        int32_t blat = 0;
        int32_t blon = 0;
        for (j = start; j < i; j++) {
          write_point(frame, &fn, &n->queue[j], has_prev, blat, blon);
          blat = n->queue[j].lat_micro;
          blon = n->queue[j].lon_micro;
          has_prev = 1;
        }
      }
      if (len + fn > body_cap) {
        break;
      }
      memcpy(body + len, frame, fn);
      len += fn;
      frames += 1;
    }
  }

  if (n->stop_pending && frames < PICKPOINT_NANO_MAX_FRAMES &&
      len < body_cap) {
    body[len++] = C_TRACK_STOP;
  }

  if (len == 0) {
    return 0;
  }
  *body_len = len;
  return 1;
}

typedef struct {
  const uint8_t *p;
  size_t n;
} rdr;

static int need(rdr *r, size_t k, const uint8_t **out) {
  if (r->n < k) {
    return -1;
  }
  *out = r->p;
  r->p += k;
  r->n -= k;
  return 0;
}

static int rd_u8(rdr *r, uint8_t *v) {
  const uint8_t *p;
  if (need(r, 1, &p) != 0) {
    return -1;
  }
  *v = p[0];
  return 0;
}
static int rd_u16(rdr *r, uint16_t *v) {
  const uint8_t *p;
  if (need(r, 2, &p) != 0) {
    return -1;
  }
  *v = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
  return 0;
}
static int rd_u32(rdr *r, uint32_t *v) {
  const uint8_t *p;
  if (need(r, 4, &p) != 0) {
    return -1;
  }
  *v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
       ((uint32_t)p[3] << 24);
  return 0;
}

static void format_uuid(const uint8_t u[16], char out[37]) {
  static const char hex[] = "0123456789abcdef";
  static const int dash_at[] = {4, 6, 8, 10};
  int di = 0;
  size_t o = 0;
  size_t i;
  for (i = 0; i < 16; i++) {
    if (di < 4 && (int)i == dash_at[di]) {
      out[o++] = '-';
      di += 1;
    }
    out[o++] = hex[u[i] >> 4];
    out[o++] = hex[u[i] & 0x0f];
  }
  out[o] = '\0';
}

static int skip_len16(rdr *r) {
  uint16_t n;
  const uint8_t *p;
  if (rd_u16(r, &n) != 0) {
    return -1;
  }
  if (n > MAX_STRING) {
    return -1;
  }
  return need(r, n, &p);
}

static int rd_str_copy(rdr *r, char *dst, size_t dst_cap) {
  uint16_t n;
  const uint8_t *p;
  size_t copy;
  if (rd_u16(r, &n) != 0) {
    return -1;
  }
  if (n > MAX_STRING) {
    return -1;
  }
  if (need(r, n, &p) != 0) {
    return -1;
  }
  copy = n;
  if (copy + 1 > dst_cap) {
    copy = dst_cap - 1;
  }
  memcpy(dst, p, copy);
  dst[copy] = '\0';
  return 0;
}

static int apply_evt(pickpoint_nano *n, uint8_t typ, rdr *r,
                     pickpoint_nano_flush_result *out) {
  if (typ == S_ACK) {
    uint32_t seq;
    size_t i;
    size_t keep = 0;
    if (rd_u32(r, &seq) != 0) {
      return -1;
    }
    if ((uint64_t)seq > n->last_acked) {
      n->last_acked = seq;
    }
    for (i = 0; i < n->qlen; i++) {
      if (n->queue[i].seq > n->last_acked) {
        n->queue[keep++] = n->queue[i];
      }
    }
    n->qlen = keep;
    out->last_acked = n->last_acked;
    return 0;
  }
  if (typ == S_TRACK_STARTED) {
    const uint8_t *uuid;
    if (need(r, 16, &uuid) != 0) {
      return -1;
    }
    if (skip_len16(r) != 0) {
      return -1;
    }
    n->started = 1;
    format_uuid(uuid, n->track_uid);
    memcpy(out->track_uid, n->track_uid, 37);
    return 0;
  }
  if (typ == S_TRACK_STOPPED) {
    const uint8_t *uuid;
    if (need(r, 16, &uuid) != 0) {
      return -1;
    }
    (void)uuid;
    n->started = 0;
    n->stop_pending = 0;
    n->track_uid[0] = '\0';
    out->track_uid[0] = '\0';
    out->stopped = 1;
    return 0;
  }
  if (typ == S_RELOCATE) {
    uint32_t retry;
    if (rd_u32(r, &retry) != 0) {
      return -1;
    }
    if (rd_str_copy(r, out->relocate_endpoint, sizeof(out->relocate_endpoint)) !=
        0) {
      return -1;
    }
    out->retry_after_ms = retry;
    return 0;
  }
  if (typ == S_ERROR) {
    uint8_t code;
    uint32_t retry;
    const uint8_t *uuid;
    if (rd_u8(r, &code) != 0 || rd_u32(r, &retry) != 0 || need(r, 16, &uuid) != 0) {
      return -1;
    }
    (void)uuid;
    if (code < 1 || code > 6) {
      return -1;
    }
    if (rd_str_copy(r, out->error_message, sizeof(out->error_message)) != 0) {
      return -1;
    }
    out->error_code = (int)code;
    out->retry_after_ms = retry;
    return 0;
  }
  return -1;
}

int pickpoint_nano_handle_response(pickpoint_nano *n, const uint8_t *body,
                                   size_t len,
                                   pickpoint_nano_flush_result *out) {
  rdr r;
  if (!n || (len > 0 && !body) || !out) {
    return PICKPOINT_NANO_ERR_ARG;
  }
  memset(out, 0, sizeof(*out));
  out->last_acked = n->last_acked;
  memcpy(out->track_uid, n->track_uid, 37);
  r.p = body;
  r.n = len;
  while (r.n > 0) {
    uint8_t typ;
    size_t before;
    if (rd_u8(&r, &typ) != 0) {
      return PICKPOINT_NANO_ERR_DECODE;
    }
    before = r.n;
    if (typ == 0x00 || typ == 0x7F || typ == 0xFF || typ == 0x8C) {
      return PICKPOINT_NANO_ERR_DECODE;
    }
    if (apply_evt(n, typ, &r, out) != 0) {
      return PICKPOINT_NANO_ERR_DECODE;
    }
    if (r.n == before && typ != C_TRACK_STOP) {
      /* consumed nothing after type — illegal */
      return PICKPOINT_NANO_ERR_DECODE;
    }
  }
  return PICKPOINT_NANO_OK;
}

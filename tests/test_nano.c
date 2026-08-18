#include "pickpoint_nano.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fails;

static void expect(int cond, const char *msg) {
  if (!cond) {
    fprintf(stderr, "FAIL: %s\n", msg);
    g_fails += 1;
  }
}

static int contains_u8(const uint8_t *b, size_t n, uint8_t v) {
  size_t i;
  for (i = 0; i < n; i++) {
    if (b[i] == v) {
      return 1;
    }
  }
  return 0;
}

static void test_empty_flush(void) {
  pickpoint_nano *n = pickpoint_nano_new("d", "s");
  char url[512];
  uint8_t body[256];
  size_t len = 99;
  expect(n != NULL, "new");
  expect(pickpoint_nano_flush_request(n, url, sizeof(url), body, sizeof(body),
                                      &len) == 0,
         "empty flush");
  pickpoint_nano_free(n);
}

static void test_flush_auth_query(void) {
  pickpoint_nano *n = pickpoint_nano_new("dev-1", "s ecret");
  char url[512];
  uint8_t body[PICKPOINT_NANO_MAX_BODY];
  size_t len = 0;
  int rc;
  expect(n != NULL, "new");
  expect(pickpoint_nano_push(n, 55.0, 37.0, 1700000000000LL) == PICKPOINT_NANO_OK,
         "push");
  rc = pickpoint_nano_flush_request(n, url, sizeof(url), body, sizeof(body), &len);
  expect(rc == 1, "flush rc");
  expect(strstr(url, PICKPOINT_NANO_DEFAULT_INGEST_URL) == url, "url prefix");
  expect(strstr(url, "client-id=dev-1") != NULL, "client-id");
  expect(strstr(url, "client-secret=s%20ecret") != NULL, "encoded secret");
  expect(len > 0 && body[0] == 0x02, "TrackStart");
  expect(contains_u8(body, len, 0x04), "Loc");
  pickpoint_nano_free(n);
}

static void test_ack_drops_prefix(void) {
  pickpoint_nano *n = pickpoint_nano_new("d", "s");
  char url[512];
  uint8_t body[PICKPOINT_NANO_MAX_BODY];
  size_t len = 0;
  uint8_t ack[5];
  pickpoint_nano_flush_result out;
  expect(pickpoint_nano_push(n, 55.0, 37.0, 0) == 0, "push1");
  expect(pickpoint_nano_push(n, 55.0001, 37.0001, 0) == 0, "push2");
  expect(pickpoint_nano_flush_request(n, url, sizeof(url), body, sizeof(body),
                                      &len) == 1,
         "flush");
  expect(pickpoint_nano_queue_len(n) == 2, "queued 2");
  ack[0] = 0x85;
  ack[1] = 1;
  ack[2] = 0;
  ack[3] = 0;
  ack[4] = 0;
  expect(pickpoint_nano_handle_response(n, ack, sizeof(ack), &out) == 0,
         "handle ack");
  expect(pickpoint_nano_last_acked(n) == 1, "acked 1");
  expect(pickpoint_nano_queue_len(n) == 1, "one left");
  pickpoint_nano_free(n);
}

static void test_overflow_drops_oldest(void) {
  pickpoint_nano *n = pickpoint_nano_new("d", "s");
  char url[512];
  uint8_t body[PICKPOINT_NANO_MAX_BODY];
  size_t len = 0;
  expect(pickpoint_nano_set_queue_cap(n, 2) == 0, "cap");
  expect(pickpoint_nano_push(n, 1.0, 2.0, 0) == 0, "p1");
  expect(pickpoint_nano_push(n, 1.0, 2.0001, 0) == 0, "p2");
  expect(pickpoint_nano_push(n, 1.0, 2.0002, 0) == 0, "p3");
  expect(pickpoint_nano_queue_len(n) == 2, "cap 2");
  expect(pickpoint_nano_flush_request(n, url, sizeof(url), body, sizeof(body),
                                      &len) == 1,
         "flush");
  expect(len > 0, "body");
  pickpoint_nano_free(n);
}

static void test_stop_appended(void) {
  pickpoint_nano *n = pickpoint_nano_new("d", "s");
  char url[512];
  uint8_t body[64];
  size_t len = 0;
  pickpoint_nano_request_stop(n);
  expect(pickpoint_nano_flush_request(n, url, sizeof(url), body, sizeof(body),
                                      &len) == 1,
         "flush stop");
  expect(len == 1 && body[0] == 0x03, "TrackStop only");
  pickpoint_nano_free(n);
}

static void test_track_started_uuid(void) {
  pickpoint_nano *n = pickpoint_nano_new("d", "s");
  uint8_t body[3 + 16];
  pickpoint_nano_flush_result out;
  size_t i;
  memset(body, 0, sizeof(body));
  body[0] = 0x83;
  for (i = 0; i < 16; i++) {
    body[1 + i] = 0x11;
  }
  body[17] = 0;
  body[18] = 0;
  expect(pickpoint_nano_handle_response(n, body, 19, &out) == 0, "started");
  expect(pickpoint_nano_track_uid(n) != NULL, "uid set");
  expect(strlen(pickpoint_nano_track_uid(n)) == 36, "uuid hyphenated");
  pickpoint_nano_free(n);
}

int main(void) {
  test_empty_flush();
  test_flush_auth_query();
  test_ack_drops_prefix();
  test_overflow_drops_oldest();
  test_stop_appended();
  test_track_started_uuid();
  if (g_fails) {
    fprintf(stderr, "%d test(s) failed\n", g_fails);
    return 1;
  }
  puts("ok");
  return 0;
}

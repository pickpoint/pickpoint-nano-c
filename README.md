# pickpoint-nano-c

Tiny **HTTP ingest** helper for Pickpoint GPS beacons, in **C99**. This is **not** a mini Pickpoint SDK: there is no geocoding, routing, listener, WebSocket, GPS filter, or HTTP client.

Same protocol as the Rust crate [`pickpoint-nano`](https://github.com/pickpoint/pickpoint-nano): concatenated [`tracking.v2`](https://github.com/pickpoint/pickpoint-proto) client frames. HTTPS is done by firmware (typically a cellular modem).

Library name: `libpickpoint_nano`. Header: `pickpoint_nano.h`. Prefix: `pickpoint_nano_*`.

```c
#include "pickpoint_nano.h"

int main(void) {
  pickpoint_nano *n = pickpoint_nano_new("device-uid", "device-secret");
  char url[512];
  uint8_t body[PICKPOINT_NANO_MAX_BODY];
  size_t len = 0;
  pickpoint_nano_flush_result res;

  /* uid/secret from EEPROM/OTP at runtime — not compile-time constants. */
  pickpoint_nano_push(n, 55.75, 37.62, 1700000000000LL);
  if (pickpoint_nano_flush_request(n, url, sizeof(url), body, sizeof(body),
                                   &len) == 1) {
    /* Modem: POST url, body, Content-Type: application/octet-stream */
    (void)pickpoint_nano_handle_response(n, /*response*/ body, 0, &res);
  }
  pickpoint_nano_free(n);
  return 0;
}
```

`flush_request` always puts `client-id` / `client-secret` in the **query string**. If the module can set headers, firmware may copy them to `X-Client-Id` / `X-Client-Secret` and strip the query.

Default endpoint: `https://tracking.pickpoint.io/v2/ingest`.

Queue default **256** (max **1024**); overflow drops the **oldest** points. No libcurl, TLS, or JSON.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Apache-2.0. Wire spec: [pickpoint-proto](https://github.com/pickpoint/pickpoint-proto).

### CI & release

- **PR to `dev`** → `.github/workflows/ci.yml` (CMake + `ctest`)
- **Merge `dev` → `main`** (untagged HEAD) → bump **patch** in `CMakeLists.txt` and `PICKPOINT_NANO_VERSION`, tag `vX.Y.Z`, GitHub Release  
  (tag push via `GITHUB_TOKEN` does not start new workflows — the release is created in the same job)
- **Manual tag `v*`** (pushed by a human) → GitHub Release after build/test

Minor/major: bump both version strings in a PR, merge with `[skip release]` in the commit message, then:

```bash
git tag v2.1.0
git push origin v2.1.0
```

First `2.0.0`: merge the initial commit with `[skip release]`, then `git tag v2.0.0 && git push origin v2.0.0`. A plain push to `main` would auto-bump to `2.0.1`.

## Contributing

Fork and open a PR against **`dev`**. [CONTRIBUTING.md](CONTRIBUTING.md).

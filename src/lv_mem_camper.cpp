// LVGL memory: a reserved pool for the screen, so Bluetooth, WiFi or the phone page can never take the RAM the
// screen needs to draw. Taken once at start (one block, no fragmentation). When the pool is full, LVGL borrows
// from the normal heap. lv_conf.h selects this with LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM.
#include <lvgl.h>
#include <string.h>
#include <stdlib.h>
#include <esp_heap_caps.h>
#include <multi_heap.h>

static constexpr size_t POOL = 40 * 1024;          // measured: ~25 KB at rest, ~35 KB peak in demo + modals
static uint8_t *base = nullptr;
static multi_heap_handle_t pool = nullptr;
static size_t spilled = 0, spilled_max = 0;

static bool in_pool(const void *p) { return base && p >= base && p < base + POOL; }

extern "C" {
void lv_mem_init(void) {
  base = (uint8_t *)heap_caps_malloc(POOL, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  if (base) pool = multi_heap_register(base, POOL);
}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void *, size_t) { return nullptr; }
void lv_mem_remove_pool(lv_mem_pool_t) {}

void *lv_malloc_core(size_t size) {
  void *p = pool ? multi_heap_malloc(pool, size) : nullptr;
  if (!p && (p = malloc(size))) { spilled += size; if (spilled > spilled_max) spilled_max = spilled; }
  return p;
}
void lv_free_core(void *p) {
  if (!p) return;
  if (in_pool(p)) multi_heap_free(pool, p);
  else { size_t n = heap_caps_get_allocated_size(p); spilled = spilled > n ? spilled - n : 0; free(p); }
}
void *lv_realloc_core(void *p, size_t n) {
  if (!p) return lv_malloc_core(n);
  if (!in_pool(p)) {                                  // a heap block stays on the heap
    size_t old = heap_caps_get_allocated_size(p);
    void *q = realloc(p, n);
    if (q) spilled = spilled - (spilled > old ? old : spilled) + n;
    return q;
  }
  void *q = multi_heap_realloc(pool, p, n);
  if (q) return q;
  q = lv_malloc_core(n);                              // pool full: move it out
  if (!q) return nullptr;
  size_t old = multi_heap_get_allocated_size(pool, p);
  memcpy(q, p, old < n ? old : n);
  multi_heap_free(pool, p);
  return q;
}
void lv_mem_monitor_core(lv_mem_monitor_t *m) {
  memset(m, 0, sizeof *m);
  if (!pool) return;
  multi_heap_info_t i; multi_heap_get_info(pool, &i);
  m->total_size = POOL;
  m->free_size = i.total_free_bytes;
  m->free_biggest_size = i.largest_free_block;
  m->max_used = POOL - i.minimum_free_bytes;
  m->used_pct = 100 - (100 * i.total_free_bytes) / POOL;
  m->frag_pct = i.total_free_bytes ? 100 - (100 * i.largest_free_block) / i.total_free_bytes : 0;
}
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }
size_t lv_mem_spilled(void) { return spilled; }
size_t lv_mem_spilled_max(void) { return spilled_max; }
}

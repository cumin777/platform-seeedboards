/* SPDX-License-Identifier: Apache-2.0 */
/* TFLM assert adapter for Zephyr — empty stub avoids link conflict. */
extern "C" void __assert_func(const char *file, int line,
                              const char *func, const char *expr) {
  (void)file; (void)line; (void)func; (void)expr;
  for (;;) {
    /* halt */
  }
}

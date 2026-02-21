#ifndef NSPIRE_LOG_HOOK_H
#define NSPIRE_LOG_HOOK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called from CPU loop to lazily scan/install hooks once OS code is live. */
void nspire_log_hook_poll(uint32_t pc);

/* Called on exec breakpoint hit. Returns true if handled and debugger should not open. */
bool nspire_log_hook_handle_exec(uint32_t pc);

/* Clears hook state and removes installed hook breakpoints when possible. */
void nspire_log_hook_reset(void);

/* Manual control from debugger console. */
void nspire_log_hook_scan_now(void);
void nspire_log_hook_set_enabled(bool enabled);
bool nspire_log_hook_is_enabled(void);
void nspire_log_hook_status(void);
void nspire_log_hook_on_memory_write(uint32_t addr, uint32_t size);
void nspire_log_hook_set_filter_bypass(bool enabled);
bool nspire_log_hook_filter_bypass_is_enabled(void);
bool nspire_log_hook_filter_bypass_is_installed(void);

#ifdef __cplusplus
}
#endif

#endif

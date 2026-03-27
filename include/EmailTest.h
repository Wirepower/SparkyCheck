#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sends a generic SMTP test email using current saved SMTP settings.
 * Returns true on success, false on failure (with optional error text).
 */
bool EmailTest_sendNow(char* err, unsigned err_size);

/**
 * Wraps email body content in a branded SparkyCheck HTML shell.
 * Use for all future outgoing SMTP mails (reports, alerts, etc).
 */
void EmailTest_buildBrandedHtml(const char* heading, const char* body_html, char* out, unsigned out_size);

/**
 * Sends a custom SMTP email using saved SMTP settings.
 * Device/Cubicle identity is appended to subject automatically when defined.
 */
bool EmailTest_sendCustomNow(const char* subject_base,
                             const char* heading,
                             const char* body_text,
                             const char* body_html_core,
                             char* err,
                             unsigned err_size);

#ifdef __cplusplus
}
#endif


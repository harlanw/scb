#ifndef SCB_H
#define SCB_H

#include <stddef.h>
#include <stdint.h>

#define CTRL_DOWN(k) ((k) & 0x1F)

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- MAIN FUNCTIONS ----------- */

// MUST be called before any other function
int
scb_init(void);

void
scb_cleanup(void);

// Flushes buffer to screen
void
scb_refresh(void);

// In-order printing to terminal buffer
size_t
scb_printf(const char *fmt, ...);

/* ---------- GET/SET FUNCTIONS ----------- */

// Sets cursor visibility to @on
void
scb_cursor(int on);

// Non-blocking character read from stdin
char
scb_getch(void);

uint16_t
scb_height(void);

uint16_t
scb_width(void);

#ifdef __cplusplus
}
#endif

#endif

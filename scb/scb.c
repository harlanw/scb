//
// SCB - Simple Console Buffer
//
// Harlan J. Waldrop <harlan@ieee.org>
// 
// https://web.engr.oregonstate.edu/~waldroha/post/teaching/virtual-console-buffering/
//
#include "scb.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

// REFERENCE LINKS
//
// POSIX Terminal (Serial) Interface:
// 	https://www.cmrr.umn.edu/%7Estrupp/serial.html
//
// Digital VT1000:
// 	https://vt100.net/docs/vt100-ug/chapter3.html
// 	https://vt100.net/docs/vt510-rm/DECTCEM.html
//
// 	ESC = \x1B = 27
//
// 	ESC [ H -- Cursor Position
// 	https://vt100.net/docs/vt100-ug/chapter3.html#CUP
//
// 	ESC [ J -- Erase In Display
// 	https://vt100.net/docs/vt100-ug/chapter3.html#ED
//
// 	ESC [ K -- Erase In Line
// 	https://vt100.net/docs/vt100-ug/chapter3.html#EL

#define SET_FLAG(var,b) ((var) |= (b))
#define CLEAR_FLAG(var,b) ((var) &= (~(b)))
#define FLAG_IS_SET(var,b) (((var) & (b)) == (b))

#define SCB_FLAG_ROW_WRAP 1
#define SCB_FLAG_CURSOR   2

// Represents an entire terminal row
struct row
{
	char *data;
	uint16_t length;
};

// Represents the terminal buffer
struct terminal
{
	// [7-2] RESERVED
	// [1]   CURSOR   - Cursori visibility (1 = ON)
	// [0]   ROW_WRAP - Set when rptr is incremented while on last row
	uint8_t flags;

	uint16_t nrows, ncols;	// Screen width, height on startup
	uint16_t rptr, cptr;	// Coordinates of next empty cell
	struct row *rows;

	struct termios original;
};

static struct terminal term;

static int
_get_screen_size(uint16_t *rows, uint16_t *cols)
{
	int status = 0;

	struct winsize ws;

	if ((ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) || (ws.ws_col == 0))
	{
		status = -1;

		*rows = 0;
		*cols = 0;
	}
	else
	{
		*rows = ws.ws_row;
		*cols = ws.ws_col;
	}

	return status;
}

static int
_raw_on(void)
{
	int status = tcgetattr(STDIN_FILENO, &term.original);

	if (status != -1)
	{
		struct termios raw = term.original;

		// The following come from POSIX terminal (serial) interface
		// definitions (see: url). But I copied them straight out of the
		// manual entry for tcsetattr (man tcsetattr).
		//
		// https://www.cmrr.umn.edu/%7Estrupp/serial.html
		raw.c_cflag |= (CS8);
		raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
		raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
		raw.c_oflag &= ~(OPOST);

		raw.c_cc[VMIN] = 0;
		raw.c_cc[VTIME] = 1;

		tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	}

	return status;
}

static int
_raw_off(void)
{
	int status = tcsetattr(STDIN_FILENO, TCSAFLUSH, &term.original);

	return status;
}

int
scb_init(void)
{
	int status;

	_raw_on();

	term.flags = 0;

	term.rptr = term.cptr = 0;
	term.rows = NULL;

	status = _get_screen_size(&term.nrows, &term.ncols);
	if (status == 0)
	{
		term.rows = calloc(term.nrows, sizeof *term.rows);
	}

	return status;
}

void
scb_cleanup(void)
{
	// Always reset cursor (pos, visibility) and clear screen
	(void) write(STDIN_FILENO, "\x1b[2J\x1b[H", 7);
	scb_cursor(1);

	if (term.rows)
	{
		uint16_t row;
		for (row = 0; row < term.nrows; row++)
			free(term.rows[row].data);
		free(term.rows);
		term.rows = NULL;
	}

	term.nrows = term.ncols = 0;

	_raw_off();
}

void
scb_refresh(void)
{
	(void) write(STDOUT_FILENO, "\x1b[H", 3);

	// Restore cursor state later
	uint8_t cursor_state = FLAG_IS_SET(term.flags, SCB_FLAG_CURSOR);
	scb_cursor(0);

	uint16_t r;
	for (r = 0; r < term.nrows - 1; r++)
	{
		struct row *row = &term.rows[r];

		(void) write(STDOUT_FILENO, "\x1b[K", 3);

		if (row->data)
		{
			(void) write(STDOUT_FILENO, row->data, row->length);

			free(row->data);
			row->data = NULL;
			row->length = 0;
		}

		if (r < term.nrows - 1)
			(void) write(STDOUT_FILENO, "\r\n", 2);
	}

	term.flags = 0;
	term.rptr = term.cptr = 0;

	scb_cursor(cursor_state);
}

size_t
scb_printf(const char *fmt, ...)
{
	va_list ap0, ap1;

	char *buffer;
	size_t length;

	// Block printf() calls after the rows are filled
	if (FLAG_IS_SET(term.flags, SCB_FLAG_ROW_WRAP))
	{
		return 0;
	}

	// vsnprintf() consumes the va_list and we need to call it twice so a
	// copy must be made
	va_start(ap0, fmt);
	va_copy(ap1, ap0);
	length = vsnprintf(NULL, 0, fmt, ap0);
	va_end(ap0);

	if (!length)
		return 0;

	buffer = malloc((length + 1) * sizeof *buffer);

	vsnprintf(buffer, length + 1, fmt, ap1);
	va_end(ap1);

	uint16_t i;
	uint16_t still_to_copy = length;
	for (i = 0; i < length; i++)
	{
		// Exit loop early when all rows have been filled
		if (FLAG_IS_SET(term.flags, SCB_FLAG_ROW_WRAP))
		{
			break;
		}

		// In the future more control characters will be added, e.g.,
		// colors, styles, etc.
		if (buffer[i] == '\n')
		{
			term.cptr = 0;

			++term.rptr;
			if (term.rptr == term.nrows)
			{
				SET_FLAG(term.flags, SCB_FLAG_ROW_WRAP);
			}

			continue;
		}

		// Reallocate row buffers ahead of time, i.e., when appending
		// new strings and when adding new rows.
		if (i == 0 || term.cptr == 0)
		{
			// Account for wrapping in allocation
			uint16_t old_len = term.rows[term.rptr].length;
			char *old_data = term.rows[term.rptr].data;

			// Determine the max cells left in row and the amount
			// we are going to copy
			uint16_t max = term.ncols - old_len;
			uint16_t cpy_len = still_to_copy;
			if (cpy_len > max)
			{
				cpy_len = max;
			}
			// Copy exact amount of chars up to the newline (if one
			// exists).
			uint16_t j;
			for (j = 0; j < cpy_len; j++)
			{
				if (buffer[i + j] == '\n')
				{
					cpy_len = j;
					--still_to_copy;
					break;
				}
			}

			still_to_copy -= cpy_len;

			char *new_data = realloc(old_data, old_len + cpy_len);
			term.rows[term.rptr].data = new_data;
			term.rows[term.rptr].length = old_len + cpy_len;
		}

		// This line is done in place of memcpy() since we do not want
		// to copy special characters like newline.
		term.rows[term.rptr].data[term.cptr] = buffer[i];

		++term.cptr;
		if (term.cptr == term.ncols)
		{
			term.cptr = 0;

			++term.rptr;
			if (term.rptr == term.nrows)
			{
				SET_FLAG(term.flags, SCB_FLAG_ROW_WRAP);
			}
		}
	}

	free(buffer);

	return length;
}

void
scb_cursor(int on)
{
	if (on)
	{
		SET_FLAG(term.flags, SCB_FLAG_CURSOR);
		(void) write(STDIN_FILENO, "\x1b[?25h", 6);
	}
	else
	{
		CLEAR_FLAG(term.flags, SCB_FLAG_CURSOR);
		(void) write(STDIN_FILENO, "\x1b[?25l", 6);
	}
}

char
scb_getch(void)
{
	char c = 0;
	(void) read(STDIN_FILENO, &c, 1);

	return c;
}

uint16_t
scb_height(void)
{
	return term.nrows;
}

uint16_t
scb_width(void)
{
	return term.ncols;
}

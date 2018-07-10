#include "scb/scb.h"

#include <stdlib.h>

int
main(void)
{
	int frame = 0;
	int close_requested = 0;

	int status = scb_init();
	if (status == -1)
		return EXIT_FAILURE;

	scb_cursor(0);

	while (close_requested == 0)
	{
		scb_refresh();

		scb_printf("[file: " __FILE__ " ]\n");
		scb_printf("[frame: %.4d ]\n", frame);

		if (frame % 10 < 5)
		{
			size_t len = sizeof("SBC 0.1 DEMO");
			size_t i;
			for (i = 0; i < (scb_width() - len) / 2; i++)
			{
				scb_printf(" ");
			}
			scb_printf("SBC 0.1 DEMO\n");
		}

		++frame;

		char c = scb_getch();
		if (c == CTRL_DOWN('q'))
		{
			close_requested = 1;
		}
	}
	scb_cleanup();

	return EXIT_SUCCESS;
}

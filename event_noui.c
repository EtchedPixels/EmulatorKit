#include <stdio.h>
#include "event.h"

void add_ui_handler(int (*handler)(void *priv, void *ev), void *private)
{
}

void remove_ui_handler(int (*handler)(void *priv, void *ev), void *private)
{
}

unsigned ui_event(void)
{
	return 0;
}

void ui_init(void)
{
}

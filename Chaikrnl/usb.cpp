#include <usb.h>

extern void xhci_init();

void setup_usb()
{
	xhci_init();
}
#include <usb.h>
#include "usb_private.h"
#include <redblack.h>

extern void xhci_init();

RedBlackTree<size_t, USBHostController*> controllers;

static size_t HandleAlloc;

void setup_usb()
{
	HandleAlloc = 0;
	xhci_init();
}

void usb_run()
{
	for (auto it = controllers.begin(); it != controllers.end(); ++it)
		it->second->init(it->first);
}

void RegisterHostController(USBHostController* hc)
{
	controllers[++HandleAlloc] = hc;
}
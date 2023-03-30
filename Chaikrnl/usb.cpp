#include <usb.h>
#include "usb_private.h"
#include <redblack.h>
#include <kstdio.h>

extern void xhci_init();

RedBlackTree<size_t, USBHostController*> controllers;

static size_t HandleAlloc;

void setup_usb()
{
	HandleAlloc = 0;
	xhci_init();
}

static void InitUsbController(USBHostController* hc, size_t handle)
{
	hc->init(handle);
	//return;
	auto& roothb = hc->RootHub();
	usb_status_t status;
	for (size_t n = 1; n <= roothb.NumberPorts(); ++n)
	{
		if (roothb.PortConnected(n))
		{
			if (USB_FAILED(status = roothb.Reset(n))) continue;
			UsbPortInfo* rootSlot;
			if (USB_FAILED(status = roothb.AssignSlot(0, n, rootSlot))) continue;
			//Now we get device descriptor
			REQUEST_PACKET device_packet;
			device_packet.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
			device_packet.request = USB_BREQUEST_GET_DESCRIPTOR;
			device_packet.value = USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_DEVICE, 0);
			device_packet.index = 0;
			device_packet.length = 8;

			volatile usb_device_descriptor* devdesc;
			if (USB_FAILED(status = roothb.RequestData(rootSlot, device_packet, (void**)&devdesc)))
				continue;

			size_t exp = devdesc->bMaxPacketSize0;
			size_t def = roothb.OperatingPacketSize(rootSlot);
			size_t pktsz = exp;
			if (devdesc->bcdUSB >> 16 > 2)
				pktsz = pow2(exp);



			if (pktsz != def)
			{
				kprintf(u"Max packet size %d does not match default %d (exp:%d)\n", pktsz, def, exp);
			}


			if (devdesc->bDeviceClass == 0x09)
				kprintf(u"USB Hub\n");

			device_packet.length = devdesc->bLength;
			kprintf(u"Device descriptor length %d\n", device_packet.length);

			if (USB_FAILED(status = roothb.RequestData(rootSlot, device_packet, (void**)&devdesc)))
			{
				continue;
			}

			volatile wchar_t* devstr;
			if (devdesc->iManufacturer != 0)
			{
				//Get device string
				device_packet.value = USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_STRING, devdesc->iManufacturer);
				device_packet.length = 256;


				if (USB_FAILED(status = roothb.RequestData(rootSlot, device_packet, (void**)&devstr)))
					continue;
				else
					kprintf(u"   Device vendor %s\n", ++devstr);
			}
			if (devdesc->iProduct != 0)
			{
				//Get device string
				device_packet.value = USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_STRING, devdesc->iProduct);
				device_packet.length = 256;

				if (USB_FAILED(status = roothb.RequestData(rootSlot, device_packet, (void**)&devstr)))
					continue;
				else
					kprintf(u"   %s\n", ++devstr);

			}
			kprintf_a("   Device %x:%x class %x:%x:%x USB version %x\n", devdesc->idVendor, devdesc->idProduct, devdesc->bDeviceClass, devdesc->bDeviceSublass, devdesc->bDeviceProtocol, devdesc->bcdUSB);

		}
	}
}

void usb_run()
{
	for (auto it = controllers.begin(); it != controllers.end(); ++it)
		InitUsbController(it->second, it->first);
}

void RegisterHostController(USBHostController* hc)
{
	size_t handle = ++HandleAlloc;
	controllers[handle] = hc;
}
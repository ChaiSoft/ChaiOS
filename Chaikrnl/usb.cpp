#include <usb.h>
#include "usb_private.h"
#include "UsbHub.h"
#include <redblack.h>
#include <kstdio.h>
#include <arch/cpu.h>

extern void xhci_init();

RedBlackTree<size_t, USBHostController*> controllers;

static size_t HandleAlloc;

void setup_usb()
{
	HandleAlloc = 0;
	xhci_init();
}

static void Stall(uint32_t milliseconds)
{
	uint64_t current = arch_get_system_timer();
	while (arch_get_system_timer() - current < milliseconds);
}

bool LogFailure(char16_t* message, usb_status_t stat)
{
	if (USB_FAILED(stat))
	{
		kprintf(message, stat);
	}
	return USB_FAILED(stat);
}

template<class T> static T* GetCompleteDescriptor(UsbDeviceInfo* device, REQUEST_PACKET& packet, size_t(*customlength)(T*) = nullptr)
{
	packet.length = 8;
	T* devstr;
	if (LogFailure(u"Failed to get descriptor size: %x\n", device->RequestData(packet, (void**)&devstr)))
	{
		if (!customlength)
			return nullptr;
		else
			devstr = nullptr;
	}
	size_t length = customlength ? customlength(devstr) : devstr->bLength;
	packet.length = length;
	if (devstr == nullptr) {
		Stall(USB_DELAY_PORT_RESET_RECOVERY);
		device->RequestData(packet, (void**)&devstr);
	}
	if (LogFailure(u"Failed to get descriptor: %x\n", device->RequestData(packet, (void**)&devstr)))
		return nullptr;
	return devstr;
}



static size_t GetConfigLength(usb_configuration_descriptor* cfg)
{
	size_t len = cfg ? cfg->wTotalLength : 71;
	return len;
}

usb_status_t ConfigureDevice(UsbDeviceInfo* device, int CONFIG, uint8_t HubPorts)
{
	usb_status_t status = USB_FAIL;
	REQUEST_PACKET device_packet;

	if (CONFIG == 0)
	{
		//Deconfigure
		//Send set configuration
		device_packet.request_type = USB_BM_REQUEST_OUTPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
		device_packet.request = USB_BREQUEST_SET_CONFIGURATION;
		device_packet.value = USB_DESCRIPTOR_WVALUE(0, CONFIG);
		device_packet.index = 0;
		device_packet.length = 0;
		void* devdesc;
		if (LogFailure(u"Failed to deconfigure device: %x\n", status = device->RequestData(device_packet, (void**)&devdesc)))
			return status;

		if (LogFailure(u"Failed to deconfigure endpoint: %x\n", status = device->ConfigureEndpoint(nullptr, CONFIG, 0, 0, HubPorts)))
			return status;

		return USB_SUCCESS;
	}

	device_packet.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
	device_packet.request = USB_BREQUEST_GET_DESCRIPTOR;
	device_packet.value = USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_CONFIGURATION, CONFIG-1);
	device_packet.index = 0;
	
	auto confdesc = GetCompleteDescriptor<usb_configuration_descriptor>(device, device_packet, &GetConfigLength);
	if (!confdesc)
		return USB_FAIL;
	usb_interface_descriptor* defiface = raw_offset<usb_interface_descriptor*>(confdesc, confdesc->bLength);
	usb_descriptor* descep = raw_offset<usb_descriptor*>(defiface, defiface->bLength);
	UsbEndpointDesc* endpointdata = new UsbEndpointDesc[defiface->bNumEndpoints + 1];
	int i = 0;
	while (raw_diff(descep, confdesc) < confdesc->wTotalLength)
	{
		if (descep->bDescriptorType == USB_DESCRIPTOR_ENDPOINT)
		{
			endpointdata[i].epDesc = (usb_endpoint_descriptor*)descep;
			endpointdata[i++].pEndpoint = nullptr;

			descep = raw_offset<usb_descriptor*>(descep, descep->bLength);

			if (descep->bDescriptorType == USB3_DESCRIPTOR_ENDPOINT_COMPANION)
			{
				endpointdata[i - 1].companionDesc = (usb3_endpoint_companion_descriptor*)descep;
			}
			else
				continue;
		}
		descep = raw_offset<usb_descriptor*>(descep, descep->bLength);
		
		
	}
	endpointdata[defiface->bNumEndpoints].epDesc = nullptr;
	endpointdata[defiface->bNumEndpoints].companionDesc = nullptr;

	if (LogFailure(u"Failed to configure endpoint: %x\n", status = device->ConfigureEndpoint(endpointdata, CONFIG, 0, 0, HubPorts)))
		return status;
	//Send set configuration
	device_packet.request_type = USB_BM_REQUEST_OUTPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
	device_packet.request = USB_BREQUEST_SET_CONFIGURATION;
	device_packet.value = USB_DESCRIPTOR_WVALUE(0, CONFIG);
	device_packet.index = 0;
	device_packet.length = 0;
	void* devdesc;
	status = device->RequestData(device_packet, (void**)&devdesc);
	if (LogFailure(u"Failed to set configuration: %x\n", status))
		return status;

	return USB_SUCCESS;
}

static volatile usb_device_descriptor* GetDeviceDescriptor(UsbDeviceInfo* rootSlot, uint8_t length)
{
	REQUEST_PACKET device_packet;
	device_packet.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
	device_packet.request = USB_BREQUEST_GET_DESCRIPTOR;
	device_packet.value = USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_DEVICE, 0);
	device_packet.index = 0;
	device_packet.length = length;
	volatile usb_device_descriptor* devstr;
	if (USB_FAILED(rootSlot->RequestData(device_packet, (void**)&devstr)))
		return nullptr;
	return devstr;
}

static volatile wchar_t* GetDeviceString(UsbDeviceInfo* rootSlot, uint8_t index)
{
	//return (volatile wchar_t*)GetStandardDescriptor(rootSlot, USB_DESCRIPTOR_STRING, index, 256);
	REQUEST_PACKET device_packet;
	volatile wchar_t* devstr;

	device_packet.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
	device_packet.request = USB_BREQUEST_GET_DESCRIPTOR;
	device_packet.value = USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_STRING, index);
	device_packet.index = 0x0409;
	device_packet.length = 256;

	if (LogFailure(u"Failed to get device string: %x\n", rootSlot->RequestData(device_packet, (void**)&devstr)))
		return nullptr;
	return devstr;
}



static void EnumerateHub(USBHub& roothb)
{
	usb_status_t status;
	if (USB_FAILED(status = roothb.Init()))
		return;
	for (size_t n = 1; n <= roothb.NumberPorts(); ++n)
	{
		if (roothb.PortConnected(n))
		{
			if (USB_FAILED(status = roothb.Reset(n))) 
				continue;
			for (int i = 0; i < roothb.HubDepth(); ++i)
				kputs(u"  ");
			kprintf(u"Reset Port %d\n", n);
			UsbDeviceInfo* rootSlot;
			if (USB_FAILED(status = roothb.AssignSlot(n, rootSlot, USB_DEVICE_INVALIDSPEED, nullptr))) continue;
			//Now we get device descriptor
			volatile usb_device_descriptor* devdesc = GetDeviceDescriptor(rootSlot, 8);
			if (!devdesc)
				continue;

			size_t exp = devdesc->bMaxPacketSize0;
			size_t def = rootSlot->OperatingPacketSize();
			size_t pktsz = exp;
			if (devdesc->bcdUSB >> 16 > 2)
				pktsz = pow2(exp);


			if (pktsz != def)
			{
				//kprintf(u"Max packet size %d does not match default %d (exp:%d)\n", pktsz, def, exp);
				if (USB_FAILED(rootSlot->UpdatePacketSize(pktsz)))
				{
					continue;
				}
			}

			//kprintf(u"Device descriptor length %d\n", devdesc->bLength);
			devdesc = GetDeviceDescriptor(rootSlot, devdesc->bLength);	

			rootSlot->SetDeviceDescriptor((usb_device_descriptor*)devdesc);

			if (!devdesc)
				continue;

			volatile wchar_t* devstr;
			if (devdesc->iManufacturer != 0)
			{
				//Get device string
				devstr = GetDeviceString(rootSlot, devdesc->iManufacturer);
				if(devstr)
					kprintf(u"   Device vendor %s\n", ++devstr);
			}
			if (devdesc->iProduct != 0)
			{
				//Get device string
				devstr = GetDeviceString(rootSlot, devdesc->iProduct);
				if (devstr)
					kprintf(u"   %s\n", ++devstr);

			}
			kprintf_a("   Device %x:%x class %x:%x:%x USB version %x\n", devdesc->idVendor, devdesc->idProduct, devdesc->bDeviceClass, devdesc->bDeviceSublass, devdesc->bDeviceProtocol, devdesc->bcdUSB);

			if (devdesc->bDeviceClass == 0x09)
			{
				NormalUsbHub* hub = new NormalUsbHub(rootSlot);
				EnumerateHub(*hub);
			}

		}
	}
}

static void InitUsbController(USBHostController* hc, size_t handle)
{
	kprintf(u"Initalizing %s controller %x\n", hc->ControllerType(), handle);
	auto& roothb = hc->RootHub();
	EnumerateHub(roothb);
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
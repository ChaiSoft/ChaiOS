#include <usb.h>
#include "usb_private.h"
#include "UsbHub.h"
#include <redblack.h>
#include <kstdio.h>
#include <arch/cpu.h>

extern void xhci_init();

RedBlackTree<size_t, USBHostController*> controllers;

static size_t HandleAlloc;

static RedBlackTree<uint32_t, usb_register_callback> usb_by_vendor;
static RedBlackTree<uint32_t, usb_register_callback> usb_by_class;

#define USB_TOKEN_BYVENDOR(vendor, device) \
	((device << 16) | vendor)

#define USB_TOKEN_BYCLASS(classcode, subclass, progif) \
	((classcode << 16) | (subclass << 8) | progif)

static bool driver_init = false;


static bool usb_driver_matcher(UsbDeviceInfo* device, uint16_t idVendor, uint16_t idProduct, uint8_t bDeviceClass, uint8_t bDeviceSublass, uint8_t bDeviceProtocol)
{
	auto vendortoken = USB_TOKEN_BYVENDOR(idVendor, idProduct);
	auto it = usb_by_vendor.find(vendortoken);
	if (it != usb_by_vendor.end())
	{
		it->second(device);
		return false;
	}

	uint32_t classtoken = USB_TOKEN_BYCLASS(bDeviceClass, bDeviceSublass, bDeviceProtocol);
	it = usb_by_class.find(classtoken);
	if (it != usb_by_class.end())
	{
		//Perfect match on classcode
		it->second(device);
		return false;
	}
	//Try class:subclass only
	classtoken = USB_TOKEN_BYCLASS(bDeviceClass, bDeviceSublass, USB_CLASS_ANY);
	it = usb_by_class.find(classtoken);
	if (it != usb_by_class.end())
	{
		//Matched
		it->second(device);
		return false;
	}
	//Try class only
	classtoken = USB_TOKEN_BYCLASS(bDeviceClass, USB_CLASS_ANY, USB_CLASS_ANY);
	it = usb_by_class.find(classtoken);
	if (it != usb_by_class.end())
	{
		//Matched
		it->second(device);
		return false;
	}
	//As a last resort, try vendor only
	vendortoken = USB_TOKEN_BYVENDOR(idVendor, USB_VENDOR_ANY);
	it = usb_by_vendor.find(vendortoken);
	if (it != usb_by_vendor.end())
	{
		it->second(device);
		return false;
	}
	//No driver
	return false;
}


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

bool LogFailure(const char16_t* message, usb_status_t stat)
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
			for (int i = 0; i < roothb.HubDepth(); ++i)
				kputs(u"  ");
			kprintf(u"Reset Port %d:", n);
			if (USB_FAILED(status = roothb.Reset(n))) 
				continue;
			kputs(u" done\n");
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
				if (devstr)
				{
					devstr[(*devstr & 0xFF) / sizeof(char16_t)] = u'\0';
					kprintf(u"   Device vendor %s\n", ++devstr);
				}
			}
			if (devdesc->iProduct != 0)
			{
				//Get device string
				devstr = GetDeviceString(rootSlot, devdesc->iProduct);
				if (devstr)
				{
					devstr[(*devstr & 0xFF) / sizeof(char16_t)] = u'\0';
					kprintf(u"   %s\n", ++devstr);
				}

			}
			kprintf_a("   Device %x:%x class %x:%x:%x USB version %x\n", devdesc->idVendor, devdesc->idProduct, devdesc->bDeviceClass, devdesc->bDeviceSublass, devdesc->bDeviceProtocol, devdesc->bcdUSB);

			if (devdesc->bDeviceClass == 0x09)
			{
				NormalUsbHub* hub = new NormalUsbHub(rootSlot);
				EnumerateHub(*hub);
			}
			else if (devdesc->bDeviceClass == 0x00)
			{
				//Interface defined
				REQUEST_PACKET device_packet;
				device_packet.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
				device_packet.request = USB_BREQUEST_GET_DESCRIPTOR;
				device_packet.value = USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_CONFIGURATION, 0);
				device_packet.index = 0;
				auto configdesc = GetCompleteDescriptor<usb_configuration_descriptor>(rootSlot, device_packet, &GetConfigLength);
				if (!configdesc)
					continue;
				usb_descriptor* desc = raw_offset<usb_descriptor*>(configdesc, configdesc->bLength);
				while (raw_diff(desc, configdesc) < configdesc->wTotalLength)
				{
					if (desc->bDescriptorType == USB_DESCRIPTOR_INTERFACE)
					{
						auto defiface = reinterpret_cast<usb_interface_descriptor*>(desc);
						kprintf(u"Interface defined device: %x:%x:%x\n", defiface->bInterfaceClass, defiface->bInterfaceSublass, defiface->bInterfaceProtocol);
						usb_driver_matcher(rootSlot, devdesc->idVendor, devdesc->idProduct, defiface->bInterfaceClass, defiface->bInterfaceSublass, defiface->bInterfaceProtocol);
					}
					desc = raw_offset<usb_descriptor*>(desc, desc->bLength);
				}
			}
			else
			{
				usb_driver_matcher(rootSlot, devdesc->idVendor, devdesc->idProduct, devdesc->bDeviceClass, devdesc->bDeviceSublass, devdesc->bDeviceProtocol);
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

static usb_device_declaration hid_devs[] = {
	{USB_VENDOR_ANY, USB_VENDOR_ANY, 0x03, 0x01, 0x01},
	USB_DEVICE_END
};

class UsbHidDriver : protected UsbEndpointInterruptHandler {
public:
	UsbHidDriver(UsbDeviceInfo* devinfo)
	{
		m_pDeviceInfo = devinfo;
	}
	usb_status_t Init();
	virtual void HandleInterrupt(uint8_t endpoint, uint64_t eventData);
private:
	UsbDeviceInfo* m_pDeviceInfo;
};

void UsbHidDriver::HandleInterrupt(uint8_t endpoint, uint64_t eventData)
{
	uint8_t* hiddata = reinterpret_cast<uint8_t*>(eventData);
	if (hiddata[2] == 0) return;
	char16_t data[2];
	data[0]  = (hiddata[2] - 0x04) + 'A';
	data[1] = 0;
	kputs(data);
}

usb_status_t UsbHidDriver::Init()
{
	usb_status_t status = USB_FAIL;
	kputs(u"USB HID init\n");
	auto devdesc = m_pDeviceInfo->GetDeviceDescriptor();

	//Try to get hub descriptor before configuring, may fail
	REQUEST_PACKET device_packet;

	const uint8_t DEFAULT_HubPorts = 4;
	uint8_t HubPorts = DEFAULT_HubPorts;

	auto CONFIG = 1;
	if (USB_FAILED(status = ConfigureDevice(m_pDeviceInfo, CONFIG, HubPorts)))
		return status;

	UsbEndpointDesc* desc;
	for (int idx = 0; desc = m_pDeviceInfo->GetEndpointIdx(idx); idx++)
	{
		if ((desc->epDesc->bmAttributes & USB_EP_ATTR_TYPE_MASK) == USB_EP_ATTR_TYPE_INTERRUPT)
		{
			break;
		}
	}
	if (desc)
	{
		void* bufdata = nullptr;
		desc->pEndpoint->CreateBuffers(&bufdata, desc->epDesc->wMaxPacketSize, 256);
		desc->pEndpoint->RegisterInterruptHandler(this);
		//kputs(u"Register hub interrupt handler\n");
	}
	return USB_SUCCESS;
}

static bool hid_usb_register(UsbDeviceInfo* devinfo)
{
	auto hid = new UsbHidDriver(devinfo);
	return !USB_FAILED(hid->Init());

}

static usb_device_registration hid_usb_reg = {
	hid_devs,
	& hid_usb_register
};

void usb_run()
{
	register_usb_driver(&hid_usb_reg);
	for (auto it = controllers.begin(); it != controllers.end(); ++it)
		InitUsbController(it->second, it->first);
}

void RegisterHostController(USBHostController* hc)
{
	size_t handle = ++HandleAlloc;
	controllers[handle] = hc;
}

CHAIKRNL_FUNC void register_usb_driver(usb_device_registration* registr)
{
	usb_device_declaration* devids = registr->ids_list;
	for (int n = 0; !(devids[n].vendor == USB_VENDOR_ANY && devids[n].dev_class == USB_CLASS_ANY); ++n)
	{
		if (devids[n].vendor != USB_VENDOR_ANY)
		{
			uint32_t token = USB_TOKEN_BYVENDOR(devids[n].vendor, devids[n].product);
			usb_by_vendor[token] = registr->callback;
		}
		else if (devids[n].dev_class != USB_CLASS_ANY)
		{
			uint32_t token = USB_TOKEN_BYCLASS(devids[n].dev_class, devids[n].subclass, devids[n].interface);
			usb_by_class[token] = registr->callback;
		}
	}
	if (driver_init)
	{
		//TODO
	}
}
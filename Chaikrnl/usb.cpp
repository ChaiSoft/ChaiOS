#include <usb.h>
#include "usb_private.h"
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

template<class T> static T* GetCompleteDescriptor(UsbDeviceInfo* device, REQUEST_PACKET& packet, size_t(*customlength)(T*) = nullptr)
{
	packet.length = 8;
	T* devstr;
	if (USB_FAILED(device->RequestData(packet, (void**)&devstr)))
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
	if (USB_FAILED(device->RequestData(packet, (void**)&devstr)))
		return nullptr;
	return devstr;
}



static size_t GetConfigLength(usb_configuration_descriptor* cfg)
{
	size_t len = cfg ? cfg->wTotalLength : 71;
	return len;
}

static usb_status_t ConfigureDevice(UsbDeviceInfo* device, int CONFIG, uint8_t HubPorts)
{
	usb_status_t status = USB_FAIL;

	REQUEST_PACKET device_packet;
	device_packet.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
	device_packet.request = USB_BREQUEST_GET_DESCRIPTOR;
	device_packet.value = USB_DESCRIPTOR_WVALUE(USB_DESCRIPTOR_CONFIGURATION, CONFIG-1);
	device_packet.index = 0;
	
	auto confdesc = GetCompleteDescriptor<usb_configuration_descriptor>(device, device_packet, &GetConfigLength);
	if (!confdesc)
		return USB_FAIL;
	usb_interface_descriptor* defiface = raw_offset<usb_interface_descriptor*>(confdesc, confdesc->bLength);
	usb_descriptor* descep = raw_offset<usb_descriptor*>(defiface, defiface->bLength);
	usb_endpoint_descriptor** endpointdata = new usb_endpoint_descriptor * [defiface->bNumEndpoints + 1];
	int i = 0;
	while (raw_diff(descep, confdesc) < confdesc->wTotalLength)
	{
		if (descep->bDescriptorType == USB_DESCRIPTOR_ENDPOINT)
		{
			endpointdata[i++] = (usb_endpoint_descriptor*)descep;
			if (i == defiface->bNumEndpoints) break;
		}
		descep = raw_offset<usb_descriptor*>(descep, descep->bLength);
	}
	endpointdata[defiface->bNumEndpoints] = nullptr;
	if (USB_FAILED(status = device->ConfigureEndpoint(endpointdata, 1, 0, 0, HubPorts)))
		return status;
	//Send set configuration
	device_packet.request_type = USB_BM_REQUEST_OUTPUT | USB_BM_REQUEST_STANDARD | USB_BM_REQUEST_DEVICE;
	device_packet.request = USB_BREQUEST_SET_CONFIGURATION;
	device_packet.value = USB_DESCRIPTOR_WVALUE(0, CONFIG);
	device_packet.index = 0;
	device_packet.length = 0;
	void* devdesc;
	if (USB_FAILED(status = device->RequestData(device_packet, (void**)&devdesc)))
		return status;
	kputs(u"Device Configured\n");
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
	device_packet.index = 0;
	device_packet.length = 256;

	if (USB_FAILED(rootSlot->RequestData(device_packet, (void**)&devstr)))
		return nullptr;
	return devstr;
}

class NormalUsbHub : public USBHub
{
public:
	NormalUsbHub(UsbDeviceInfo* pDevice)
		:m_pDevice(pDevice),
		m_parent(*m_pDevice->GetParent())
	{

	}
	virtual usb_status_t Init()
	{
		usb_status_t status = USB_FAIL;
		kputs(u"USB hub init\n");
		auto devdesc = m_pDevice->GetDeviceDescriptor();

		//Try to get hub descriptor before configuring, may fail
		REQUEST_PACKET device_packet;
		device_packet.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_CLASS | USB_BM_REQUEST_DEVICE;
		device_packet.request = USB_BREQUEST_GET_DESCRIPTOR;
		device_packet.value = USB_DESCRIPTOR_WVALUE(HUB_DESCRIPTOR, 0);
		device_packet.index = 0;
		volatile usb_hub_descriptor* data = GetCompleteDescriptor<volatile usb_hub_descriptor>(m_pDevice, device_packet);
		uint8_t HubPorts;
		if(!data)
		{
			HubPorts = 4;
		}
		else
		{
			HubPorts = data->NumPorts;
		}
		m_NumPorts = HubPorts;

		auto CONFIG = 1;
		if (USB_FAILED(status = ConfigureDevice(m_pDevice, CONFIG, HubPorts)))
			return status;

		for (int i = 1; i <= m_NumPorts; ++i)
		{
			if(!SetFeature(m_pDevice, PORT_POWER, i))
				continue;
		}
		for (int i = 1; i <= m_NumPorts; ++i)
		{
			if (!SetFeature(m_pDevice, C_PORT_CONNECTION, i, CLEAR_FEATURE))
				continue;
			/*if (!SetFeature(m_pDevice, C_PORT_ENABLE, i, CLEAR_FEATURE))
				continue;*/
		}
		
		

		/*auto confdesc = (usb_configuration_descriptor*)GetStandardDescriptor(m_pDevice, USB_DESCRIPTOR_CONFIGURATION, 1, 9);
		confdesc = (usb_configuration_descriptor*)GetStandardDescriptor(m_pDevice, USB_DESCRIPTOR_CONFIGURATION, 1, confdesc->wTotalLength);*/
		return USB_SUCCESS;
	}
	virtual usb_status_t Reset(uint8_t port)
	{
		usb_status_t status = USB_FAIL;
		if (!SetFeature(m_pDevice, PORT_RESET, port))
			return USB_FAIL;
		int i = 10;
		for (; i > 0; --i)
		{
			Stall(USB_DELAY_PORT_RESET);
			uint32_t* status = (uint32_t*)GetFeature(m_pDevice, GET_STATUS, port, 4);
			if (!status)
				return USB_FAIL;
			if ((*status & PORT_STATUS_RESET) == 0)
				break;
		}
		if (i == 0)
			return USB_FAIL;

		if (!SetFeature(m_pDevice, C_PORT_RESET, port, CLEAR_FEATURE))
			return USB_FAIL;
		//if (!SetFeature(m_pDevice, PORT_ENABLE, port));
		Stall(USB_DELAY_PORT_RESET_RECOVERY);
		kprintf(u"Reset Port %d\n", port);
		return USB_SUCCESS;
	}
	virtual bool PortConnected(uint8_t port)
	{
		uint32_t* status = (uint32_t*)GetFeature(m_pDevice, GET_STATUS, port, 4);
		if (status == nullptr) return false;
		return (*status & 1) != 0;
	}
	virtual uint8_t NumberPorts()
	{
		return m_NumPorts;
	}
	virtual usb_status_t AssignSlot(uint8_t port, UsbDeviceInfo*& slot, uint32_t PortSpeed, UsbDeviceInfo* parent, uint32_t routestring = 0)
	{
		if (PortSpeed == USB_DEVICE_INVALIDSPEED)
		{
			uint32_t* status = (uint32_t*)GetFeature(m_pDevice, GET_STATUS, port, 4);
			if (*status & (1 << 10))
				PortSpeed = USB_DEVICE_HIGHSPEED;
			else if(*status & (1 << 9))
				PortSpeed = USB_DEVICE_LOWSPEED;
			else
				PortSpeed = USB_DEVICE_FULLSPEED;
		}
		kprintf(u"PortSpeed: %d\n", PortSpeed);
		return m_parent.AssignSlot(m_pDevice->GetPort(), slot, PortSpeed, parent? parent: m_pDevice, (routestring << 4) | (port&0xF));
	}
private:
	UsbDeviceInfo* const m_pDevice;
	USBHub& m_parent;
	uint8_t m_NumPorts;

#pragma pack(push, 1)
	struct usb_hub_descriptor : public usb_descriptor {
		uint8_t NumPorts;
		uint16_t HubChars;
		uint8_t PotPGT;
		uint8_t MaxHubCurrent;
	};
#pragma pack(pop)

	enum HUB_COMMAND {
		GET_STATUS = 0,
		CLEAR_FEATURE = 1,
		SET_FEATURE=3,
		GET_DESCRIPTOR=6,
		SET_DESCRIPTOR=7,
		CLEAR_TT_BUFFER=8,
		RESET_TT=9,
		GET_TT_STATE=10,
		STOP_TT=11,
		SET_HUB_DEPTH=12,
		GET_PORT_ERR_COUNT=13
	};

	enum HUB_FEATURE {
		PORT_CONNECTION = 0,
		PORT_ENABLE = 1,
		PORT_SUSPEND = 2,
		PORT_OVER_CURRENT = 3,
		PORT_RESET = 4,
		PORT_LINK_STATE = 5,
		PORT_POWER = 8,
		PORT_LOW_SPEED = 9,
		C_PORT_CONNECTION = 0x10,
		C_PORT_ENABLE = 0x11,
		C_PORT_RESET = 0x14

	};

	static volatile void* GetFeature(UsbDeviceInfo* rootSlot, HUB_COMMAND cmd, uint8_t port, size_t length)
	{
		REQUEST_PACKET device_packet;
		void* devstr;

		device_packet.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_CLASS | USB_BM_REQUEST_OTHER;
		device_packet.request = cmd;
		device_packet.value = USB_DESCRIPTOR_WVALUE(0, 0);
		device_packet.index = port;
		device_packet.length = length;

		if (USB_FAILED(rootSlot->RequestData(device_packet, &devstr)))
			return nullptr;
		return devstr;
	}

	static volatile void* SetFeature(UsbDeviceInfo* rootSlot, HUB_FEATURE feature, uint8_t port, HUB_COMMAND cmd = SET_FEATURE)
	{
		REQUEST_PACKET device_packet;
		void* devstr;

		device_packet.request_type = USB_BM_REQUEST_OUTPUT | USB_BM_REQUEST_CLASS | USB_BM_REQUEST_OTHER;
		device_packet.request = cmd;
		device_packet.value = USB_DESCRIPTOR_WVALUE(0, feature);
		device_packet.index = port;
		device_packet.length = 0;

		if (USB_FAILED(rootSlot->RequestData(device_packet, &devstr)))
			return nullptr;
		return devstr;
	}

	static const uint8_t HUB_DESCRIPTOR = 0x29;
	static const  uint32_t PORT_STATUS_RESET = (1 << 4);

};

static void EnumerateHub(USBHub& roothb)
{
	usb_status_t status;
	if (USB_FAILED(status = roothb.Init()))
		return;
	for (size_t n = 1; n <= roothb.NumberPorts(); ++n)
	{
		if (roothb.PortConnected(n))
		{
			if (USB_FAILED(status = roothb.Reset(n))) continue;
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
				kprintf(u"Max packet size %d does not match default %d (exp:%d)\n", pktsz, def, exp);
				if (USB_FAILED(rootSlot->UpdatePacketSize(pktsz)))
				{
					continue;
				}
			}


			if (devdesc->bDeviceClass == 0x09)
				kprintf(u"USB Hub\n");

			kprintf(u"Device descriptor length %d\n", devdesc->bLength);
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
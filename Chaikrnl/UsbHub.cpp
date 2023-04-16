#include <usb.h>
#include "usb_private.h"
#include "UsbHub.h"
#include <kstdio.h>
#include <arch/cpu.h>


#pragma pack(push, 1)
struct usb_hub_descriptor : public usb_descriptor {
	uint8_t NumPorts;
	uint16_t HubChars;
	uint8_t PotPGT;
	uint8_t MaxHubCurrent;
};

struct usb3_hub_descriptor : public usb_descriptor {
	uint8_t NumPorts;
	uint16_t HubChars;
	uint8_t PotPGT;
	uint8_t MaxHubCurrent;
	uint8_t HubHeaderDecodeLatency;
	uint16_t HubDelay;
	uint16_t Removable;
};
#pragma pack(pop)

enum HUB_COMMAND {
	GET_STATUS = 0,
	CLEAR_FEATURE = 1,
	SET_FEATURE = 3,
	GET_DESCRIPTOR = 6,
	SET_DESCRIPTOR = 7,
	CLEAR_TT_BUFFER = 8,
	RESET_TT = 9,
	GET_TT_STATE = 10,
	STOP_TT = 11,
	SET_HUB_DEPTH = 12,
	GET_PORT_ERR_COUNT = 13
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

	if (LogFailure(u"Failed to get port feature: %x\n", rootSlot->RequestData(device_packet, &devstr)))
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

	if (LogFailure(u"Failed to set port feature: %x\n", rootSlot->RequestData(device_packet, &devstr)))
		return nullptr;
	return devstr;
}

static const uint8_t HUB_DESCRIPTOR = 0x29;
static const uint8_t HUB_DESCRIPTOR_3 = 0x2A;
static const  uint32_t PORT_STATUS_RESET = (1 << 4);


usb_status_t NormalUsbHub::Init()
{
	usb_status_t status = USB_FAIL;
	kputs(u"USB hub init\n");
	auto devdesc = m_pDevice->GetDeviceDescriptor();

	//Try to get hub descriptor before configuring, may fail
	REQUEST_PACKET device_packet;

	const uint8_t DEFAULT_HubPorts = 4;
	uint8_t HubPorts = DEFAULT_HubPorts;

	auto CONFIG = 1;
	if (USB_FAILED(status = ConfigureDevice(m_pDevice, CONFIG, HubPorts)))
		return status;

	if (devdesc->bDeviceProtocol >= 3)
	{
		//Have to set hub depth first
		device_packet.request_type = USB_BM_REQUEST_OUTPUT | USB_BM_REQUEST_CLASS | USB_BM_REQUEST_DEVICE;
		device_packet.request = SET_HUB_DEPTH;
		device_packet.value = m_parent.HubDepth();
		device_packet.index = 0;
		device_packet.length = 0;
		void* discard = nullptr;
		if (LogFailure(u"Failed to set USB3 hub depth: %x\n", status = m_pDevice->RequestData(device_packet, &discard)))
			return status;

		device_packet.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_CLASS | USB_BM_REQUEST_DEVICE;
		device_packet.request = USB_BREQUEST_GET_DESCRIPTOR;
		device_packet.value = USB_DESCRIPTOR_WVALUE(HUB_DESCRIPTOR, 0);
		device_packet.index = 0;
		device_packet.length = 12;
		volatile usb3_hub_descriptor* data = nullptr;

		if (LogFailure(u"Failed to get USB3 hub descriptor: %x\n", status = m_pDevice->RequestData(device_packet, (void**)&data)))
			return status;
		HubPorts = data->NumPorts;
	}
	else
	{
		device_packet.request_type = USB_BM_REQUEST_INPUT | USB_BM_REQUEST_CLASS | USB_BM_REQUEST_DEVICE;
		device_packet.request = USB_BREQUEST_GET_DESCRIPTOR;
		device_packet.value = USB_DESCRIPTOR_WVALUE(HUB_DESCRIPTOR, 0);
		device_packet.index = 0;
		device_packet.length = 71;
		volatile usb_hub_descriptor* data = nullptr;
		if (LogFailure(u"Failed to get USB2 hub descriptor: %x\n", status = m_pDevice->RequestData(device_packet, (void**)&data)))
			return status;
		HubPorts = data->NumPorts;
	}
	if (HubPorts != DEFAULT_HubPorts)
	{
		kprintf(u"Reconfigure: %d ports, not 4\n", HubPorts);
		if (USB_FAILED(status = ConfigureDevice(m_pDevice, 0, HubPorts)))
			return status;
		if (USB_FAILED(status = ConfigureDevice(m_pDevice, CONFIG, HubPorts)))
			return status;
	}


	m_NumPorts = HubPorts;


	for (int i = 1; i <= m_NumPorts; ++i)
	{
		if (!SetFeature(m_pDevice, PORT_POWER, i))
			continue;
	}
	for (int i = 1; i <= m_NumPorts; ++i)
	{
		if (!SetFeature(m_pDevice, C_PORT_CONNECTION, i, CLEAR_FEATURE))
			continue;
		/*if (!SetFeature(m_pDevice, C_PORT_ENABLE, i, CLEAR_FEATURE))
			continue;*/
	}

	UsbEndpointDesc* desc;
	for (int idx = 0; desc = m_pDevice->GetEndpointIdx(idx); idx++)
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
		kputs(u"Register hub interrupt handler\n");
	}
	/*auto confdesc = (usb_configuration_descriptor*)GetStandardDescriptor(m_pDevice, USB_DESCRIPTOR_CONFIGURATION, 1, 9);
	confdesc = (usb_configuration_descriptor*)GetStandardDescriptor(m_pDevice, USB_DESCRIPTOR_CONFIGURATION, 1, confdesc->wTotalLength);*/
	return USB_SUCCESS;
}

usb_status_t NormalUsbHub::Reset(uint8_t port)
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
	return USB_SUCCESS;
}

bool NormalUsbHub::PortConnected(uint8_t port)
{
	uint32_t* status = (uint32_t*)GetFeature(m_pDevice, GET_STATUS, port, 4);
	if (status == nullptr) return false;
	return (*status & 1) != 0;
}

usb_status_t NormalUsbHub::DoAssignSlot(uint8_t port, UsbDeviceInfo*& slot, uint32_t PortSpeed, UsbDeviceInfo* parent, uint32_t routestring)
{
	if (PortSpeed == USB_DEVICE_INVALIDSPEED)
	{
		uint32_t* status = (uint32_t*)GetFeature(m_pDevice, GET_STATUS, port, 4);
		if (*status & (1 << 10))
			PortSpeed = USB_DEVICE_HIGHSPEED;
		else if (*status & (1 << 9))
			PortSpeed = USB_DEVICE_LOWSPEED;
		else
			PortSpeed = USB_DEVICE_FULLSPEED;
	}
	if (parent == nullptr)
	{
		parent = m_pDevice;
	}
	return m_parent.AssignSlot(m_pDevice->GetPort(), slot, PortSpeed, parent, (routestring << 4) | (port & 0xF));
}

void NormalUsbHub::HandleInterrupt(uint8_t endpoint)
{
	kprintf(u"Hub status interrupt\n");
}
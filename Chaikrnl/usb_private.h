#ifndef CHAIOS_USB_PRIVATE_H
#define CHAIOS_USB_PRIVATE_H

#include <stdint.h>
#include <usb.h>

#define USB_BM_REQUEST_OUTPUT 0
#define USB_BM_REQUEST_INPUT 0x80
#define USB_BM_REQUEST_STANDARD 0
#define USB_BM_REQUEST_CLASS 0x20
#define USB_BM_REQUEST_VENDOR 0x40
#define USB_BM_REQUEST_DEVICE 0
#define USB_BM_REQUEST_INTERFACE 1
#define USB_BM_REQUEST_ENDPOINT 2
#define USB_BM_REQUEST_OTHER 3
#define USB_BM_REQUEST_VENDORSPEC 0x1F

#define USB_BREQUEST_GET_STATUS 0
#define USB_BREQUEST_CLEAR_FEATURE 1
#define USB_BREQUEST_SET_FEATURE 3
#define USB_BREQUEST_SET_ADDRESS 5
#define USB_BREQUEST_GET_DESCRIPTOR 6
#define USB_BREQUEST_SET_DESCRIPTOR 7
#define USB_BREQUEST_GET_CONFIGURATION 8
#define USB_BREQUEST_SET_CONFIGURATION 9
#define USB_BREQUEST_GET_INTERFACE 10
#define USB_BREQUEST_SET_INTERFACE 11

#define USB_DESCRIPTOR_DEVICE 1
#define USB_DESCRIPTOR_CONFIGURATION 2
#define USB_DESCRIPTOR_STRING 3
#define USB_DESCRIPTOR_INTERFACE 4
#define USB_DESCRIPTOR_ENDPOINT 5
#define USB3_DESCRIPTOR_ENDPOINT_COMPANION 0x30

#define USB_DESCRIPTOR_WVALUE(type, index) \
((type << 8) | index)

#define USB_EP_ATTR_TYPE_MASK 3
#define USB_EP_ATTR_TYPE_CONTROL 0
#define USB_EP_ATTR_TYPE_ISOCHRONOUS 1
#define USB_EP_ATTR_TYPE_BULK 2
#define USB_EP_ATTR_TYPE_INTERRUPT 3


#define USB_DELAY_PORT_RESET			50
#define USB_DELAY_PORT_RESET_RECOVERY	50

#define USB_SUCCESS 0
#define USB_FAIL 1
#define USB_NOTIMPL 2

#define USB_XHCI_TRB_ERR(x) (x + 0x1000)

#define USB_FAILED(st) (st != USB_SUCCESS)

#define USB_DEVICE_INVALIDSPEED 0
#define USB_DEVICE_FULLSPEED 1
#define USB_DEVICE_LOWSPEED 2
#define USB_DEVICE_HIGHSPEED 3
#define USB_DEVICE_SUPERSPEED 4
#define USB_DEVICE_SUPERSPEEDPLUS 5

class USBHub : protected UsbDeviceStatusNotify {
public:
	virtual usb_status_t Init() = 0;
	virtual usb_status_t Reset(uint8_t port) = 0;
	virtual bool PortConnected(uint8_t port) = 0;
	virtual uint8_t NumberPorts() = 0;
	
	virtual uint8_t HubDepth() = 0;

	usb_status_t AssignSlot(uint8_t port, UsbDeviceInfo*& slot, uint32_t PortSpeed, UsbDeviceInfo* parent, uint32_t routestring = 0)
	{
		auto status = DoAssignSlot(port, slot, PortSpeed, parent, routestring);
		m_Children[port] = slot;
		return status;
	}

	virtual void Detatch()
	{
		for (auto child : m_Children)
		{
			child.second->NotifyDisconnect();
		}
	}

	UsbDeviceInfo* GetChild(uint8_t port)
	{
		auto it = m_Children.find(port);
		if (it == m_Children.end()) return nullptr;
		return it->second;
	}

protected:
	virtual usb_status_t DoAssignSlot(uint8_t port, UsbDeviceInfo*& slot, uint32_t PortSpeed, UsbDeviceInfo* parent, uint32_t routestring = 0) = 0;
private:
	RedBlackTree<uint8_t, UsbDeviceInfo*> m_Children;
};

class USBRootHub : public USBHub
{

};



class USBHostController {
public:
	virtual const char16_t* ControllerType() = 0;
	virtual USBHub& RootHub() = 0;
};

void RegisterHostController(USBHostController* hc);
usb_status_t ConfigureDevice(UsbDeviceInfo* device, int CONFIG, uint8_t HubPorts);
bool LogFailure(const char16_t* message, usb_status_t stat);

static inline size_t pow2(size_t p)
{
	size_t result = 1;
	while (p--)
		result *= 2;
	return result;
}

#endif

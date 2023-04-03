#ifndef CHAIOS_USB_PRIVATE_H
#define CHAIOS_USB_PRIVATE_H

#include <stdint.h>

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

#define USB_DESCRIPTOR_WVALUE(type, index) \
((type << 8) | index)

#define USB_EP_ATTR_TYPE_MASK 3
#define USB_EP_ATTR_TYPE_CONTROL 0
#define USB_EP_ATTR_TYPE_ISOCHRONOUS 1
#define USB_EP_ATTR_TYPE_BULK 2
#define USB_EP_ATTR_TYPE_INTERRUPT 3


#define USB_DELAY_PORT_RESET			50
#define USB_DELAY_PORT_RESET_RECOVERY	50



struct REQUEST_PACKET {
	uint8_t  request_type;
	uint8_t  request;
	uint16_t value;
	uint16_t index;
	uint16_t length;
};

#pragma pack(push, 1)
typedef struct _usb_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
}usb_descriptor;

typedef struct _usb_device_descriptor : public usb_descriptor {
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSublass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
}usb_device_descriptor;

typedef struct _usb_configuration_descriptor : public usb_descriptor {
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t bMaxPower;
}usb_configuration_descriptor;

typedef struct _usb_interface_descriptor : public usb_descriptor {
	uint8_t bInterfaceNum;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSublass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
}usb_interface_descriptor;

typedef struct _usb_endpoint_descriptor : public usb_descriptor {
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
}usb_endpoint_descriptor;
#pragma pack(pop)

typedef uint32_t usb_status_t;

#define USB_SUCCESS 0
#define USB_FAIL 1
#define USB_NOTIMPL 2

#define USB_FAILED(st) (st != USB_SUCCESS)

#define USB_DEVICE_INVALIDSPEED 0
#define USB_DEVICE_FULLSPEED 1
#define USB_DEVICE_LOWSPEED 2
#define USB_DEVICE_HIGHSPEED 3
#define USB_DEVICE_SUPERSPEED 4
#define USB_DEVICE_SUPERSPEEDPLUS 5

class USBHub;

class UsbDeviceInfo {
public:
	UsbDeviceInfo(USBHub& parent, uint8_t port)
		:m_Parent(parent)
	{
		m_Port = port;
		m_deviceDescriptor = nullptr;
	}
	virtual size_t OperatingPacketSize() = 0;
	virtual usb_status_t UpdatePacketSize(size_t size) = 0;
	virtual usb_status_t RequestData(REQUEST_PACKET& device_packet, void** resultData) = 0;
	virtual usb_status_t ConfigureEndpoint(usb_endpoint_descriptor** pEndpoints, uint8_t config, uint8_t interface, uint8_t alternate, uint8_t downstreamports) = 0;
	virtual USBHub* GetParent() { return &m_Parent; }
	virtual uint8_t GetPort() { return m_Port; }

	usb_device_descriptor* GetDeviceDescriptor() { return m_deviceDescriptor; }
	void SetDeviceDescriptor(usb_device_descriptor* devdesc) { m_deviceDescriptor = devdesc; }

private:
	usb_device_descriptor* m_deviceDescriptor;
	USBHub& m_Parent;
	uint8_t m_Port;
};

class USBHub {
public:
	virtual usb_status_t Init() = 0;
	virtual usb_status_t Reset(uint8_t port) = 0;
	virtual bool PortConnected(uint8_t port) = 0;
	virtual uint8_t NumberPorts() = 0;
	virtual usb_status_t AssignSlot(uint8_t port, UsbDeviceInfo*& slot, uint32_t PortSpeed, UsbDeviceInfo* parent, uint32_t routestring = 0) = 0;
};

class USBRootHub : public USBHub
{

};

class USBHostController {
public:
	virtual char16_t* ControllerType() = 0;
	virtual USBHub& RootHub() = 0;
};

void RegisterHostController(USBHostController* hc);

static inline size_t pow2(size_t p)
{
	size_t result = 1;
	while (p--)
		result *= 2;
	return result;
}

#endif

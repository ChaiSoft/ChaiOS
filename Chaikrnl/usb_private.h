#ifndef CHAIOS_USB_PRIVATE_H
#define CHAIOS_USB_PRIVATE_H

#include <stdint.h>

#define USB_BM_REQUEST_INPUT 0x80
#define USB_BM_REQUEST_STANDARD 0
#define USB_BM_REQUEST_CLASS 0x20
#define USB_BM_REQUEST_VENDOR 0x40
#define USB_BM_REQUEST_DEVICE 0
#define USB_BM_REQUEST_INTERFACE 1
#define USB_BM_REQUEST_ENDPOINT 2
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


struct REQUEST_PACKET {
	uint8_t  request_type;
	uint8_t  request;
	uint16_t value;
	uint16_t index;
	uint16_t length;
};

#pragma pack(push, 1)
typedef struct _usb_device_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
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
#pragma pack(pop)

typedef uint32_t usb_status_t;

#define USB_SUCCESS 0
#define USB_FAIL 1

#define USB_FAILED(st) (st != USB_SUCCESS)

//Opaque class
class UsbPortInfo {

};

class USBHub {
public:
	virtual usb_status_t Reset(uint8_t port) = 0;
	virtual bool PortConnected(uint8_t port) = 0;
	virtual uint8_t NumberPorts() = 0;
	virtual usb_status_t AssignSlot(uint32_t routestring, uint8_t port, UsbPortInfo*& slot) = 0;
	virtual size_t OperatingPacketSize(UsbPortInfo*& slot) = 0;
	virtual usb_status_t ConfigureHub(UsbPortInfo*& slot, uint8_t downstreamports) = 0;
	virtual usb_status_t RequestData(UsbPortInfo*& slot, REQUEST_PACKET& device_packet, void** resultData) = 0;
};

class USBHostController {
public:
	virtual void init(size_t handle) = 0;
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

#ifndef CHAIOS_USB_H
#define CHAIOS_USB_H
#include <stdheaders.h>
#include <arch/cpu.h>
#include "redblack.h"
#include "linkedlist.h"

typedef uint32_t usb_status_t;

#define USB_VENDOR_ANY 0xFFFF
#define USB_CLASS_ANY 0x00
struct usb_device_declaration {
	uint16_t vendor;
	uint16_t product;
	uint8_t dev_class;
	uint8_t subclass;
	uint8_t interface;
};

#define USB_DEVICE_END {USB_VENDOR_ANY, USB_VENDOR_ANY, USB_CLASS_ANY, USB_CLASS_ANY, USB_CLASS_ANY}

class USBHub;

class UsbEndpointInterruptHandler {
public:
	virtual void HandleInterrupt(uint8_t endpoint, uint64_t eventData) = 0;
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

typedef struct _usb3_endpoint_companion_descriptor : public usb_descriptor {
	uint8_t bMaxBurst;
	union {
		uint8_t bmAttributes;
		struct {
			uint8_t MaxStream : 5;
			uint8_t ResZ : 3;
		}Bulk;
		struct {
			uint8_t Mult : 2;
			uint8_t ResZ : 6;
		}Isochronous;
	};
	uint16_t BytesPerInterval;
}usb3_endpoint_companion_descriptor;
static_assert(sizeof(usb3_endpoint_companion_descriptor) == 6, "Packing error - usb3_endpoint_companion_descriptor");

struct REQUEST_PACKET {
	uint8_t  request_type;
	uint8_t  request;
	uint16_t value;
	uint16_t index;
	uint16_t length;
};
#pragma pack(pop)


class UsbEndpoint {
public:
	virtual usb_status_t CreateBuffers(void** bufferptr, size_t bufsize, size_t bufcount) = 0;
	virtual usb_status_t AddBuffer(void* buffer, size_t length) = 0;
	virtual void RegisterInterruptHandler(UsbEndpointInterruptHandler* handler) = 0;
};

class UsbDeviceStatusNotify {
public:
	virtual void Detatch() = 0;
	static linked_list_node<UsbDeviceStatusNotify*>& NodeConv(UsbDeviceStatusNotify* ptr)
	{
		return ptr->listNode;
	}
private:
	linked_list_node<UsbDeviceStatusNotify*> listNode;
};

typedef struct _UsbEndpointDesc {
	usb_endpoint_descriptor* epDesc;
	usb3_endpoint_companion_descriptor* companionDesc;
	UsbEndpoint* pEndpoint;
}UsbEndpointDesc;


class UsbDeviceInfo {
public:
	UsbDeviceInfo(USBHub& parent, uint8_t port)
		:m_Parent(parent),
		m_NotifyChange(&UsbDeviceStatusNotify::NodeConv)
	{
		m_Port = port;
		m_deviceDescriptor = nullptr;
	}
	virtual size_t OperatingPacketSize() = 0;
	virtual usb_status_t UpdatePacketSize(size_t size) = 0;
	virtual usb_status_t RequestData(REQUEST_PACKET& device_packet, void** resultData) = 0;

	virtual USBHub* GetParent() { return &m_Parent; }
	virtual uint8_t GetPort() { return m_Port; }

	usb_device_descriptor* GetDeviceDescriptor() { return m_deviceDescriptor; }
	void SetDeviceDescriptor(usb_device_descriptor* devdesc) { m_deviceDescriptor = devdesc; }

	virtual usb_status_t ConfigureEndpoint(UsbEndpointDesc* pEndpoints, uint8_t config, uint8_t interface, uint8_t alternate, uint8_t downstreamports)
	{
		m_pDescs = pEndpoints;
		return DoConfigureEndpoint(pEndpoints, config, interface, alternate, downstreamports);
	}

	UsbEndpointDesc* GetEndpointIdx(uint8_t idx)
	{
		return &m_pDescs[idx];
	}

	void RegisterStatusCallback(UsbDeviceStatusNotify* notify)
	{
		m_NotifyChange.insert(notify);
	}

	/*void RegisterEndpoint(UsbEndpoint* endpoint, uint8_t Address)
	{
		m_Endpoints[Address] = endpoint;
	}

	UsbEndpoint* GetEndpoint(uint8_t endpoint, bool in)
	{
		uint8_t index = (in ? (1 << 7) : 0) | endpoint;
		return GetEndpoint(index);
	}

	UsbEndpoint* GetEndpoint(uint8_t endpointAddress)
	{
		return m_Endpoints[endpointAddress];

	}*/
	void NotifyDisconnect()
	{
		for (auto i : m_NotifyChange)
		{
			i->Detatch();
		}
	}
protected:
	virtual usb_status_t DoConfigureEndpoint(UsbEndpointDesc* pEndpoints, uint8_t config, uint8_t interface, uint8_t alternate, uint8_t downstreamports) = 0;

private:
	usb_device_descriptor* m_deviceDescriptor;
	USBHub& m_Parent;
	uint8_t m_Port;
	UsbEndpointDesc* m_pDescs;
	LinkedList<UsbDeviceStatusNotify*> m_NotifyChange;
	//RedBlackTree<uint8_t, UsbEndpoint*> m_Endpoints;
};


typedef bool(*usb_scan_callback)(UsbDeviceInfo* device);
typedef usb_scan_callback usb_register_callback;

struct usb_device_registration {
	usb_device_declaration* ids_list;
	usb_register_callback callback;
};

CHAIKRNL_FUNC void register_usb_driver(usb_device_registration* registr);

void setup_usb();
void usb_run();

#endif

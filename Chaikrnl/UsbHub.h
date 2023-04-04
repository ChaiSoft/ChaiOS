#ifndef CHAIOS_USB_HUB_H
#define CHAIOS_USB_HUB_H

#include <usb.h>

class NormalUsbHub : public USBHub
{
public:
	NormalUsbHub(UsbDeviceInfo* pDevice)
		:m_pDevice(pDevice),
		m_parent(*m_pDevice->GetParent())
	{

	}
	virtual uint8_t HubDepth()
	{
		return m_parent.HubDepth() + 1;
	}
	virtual usb_status_t Init();
	virtual usb_status_t Reset(uint8_t port);
	virtual bool PortConnected(uint8_t port);
	virtual uint8_t NumberPorts()
	{
		return m_NumPorts;
	}
	virtual usb_status_t AssignSlot(uint8_t port, UsbDeviceInfo*& slot, uint32_t PortSpeed, UsbDeviceInfo* parent, uint32_t routestring = 0);
private:
	UsbDeviceInfo* const m_pDevice;
	USBHub& m_parent;
	uint8_t m_NumPorts;
};

#endif
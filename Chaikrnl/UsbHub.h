#ifndef CHAIOS_USB_HUB_H
#define CHAIOS_USB_HUB_H

#include <usb.h>
#include "usb_private.h"

class NormalUsbHub : public USBHub, protected UsbEndpointInterruptHandler
{
public:
	NormalUsbHub(UsbDeviceInfo* pDevice)
		:m_pDevice(pDevice),
		m_parent(*m_pDevice->GetParent())
	{
		//m_pDevice->RegisterStatusCallback(this);
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
	virtual void HandleInterrupt(uint8_t endpoint);
protected:
	virtual usb_status_t DoAssignSlot(uint8_t port, UsbDeviceInfo*& slot, uint32_t PortSpeed, UsbDeviceInfo* parent, uint32_t routestring = 0);
private:
	UsbDeviceInfo* const m_pDevice;
	USBHub& m_parent;
	uint8_t m_NumPorts;
};



#endif
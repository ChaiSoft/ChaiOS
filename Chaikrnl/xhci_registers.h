#ifndef CHAIOS_XHCI_REGISTERS_H
#define CHAIOS_XHCI_REGISTERS_H

#include <stdint.h>
#include <pmmngr.h>

#define XHCI_CAPREG_CAPLENGTH 0x0
#define XHCI_CAPREG_HCIVERSION 0x2
#define XHCI_CAPREG_HCSPARAMS1 0x4
#define XHCI_CAPREG_HCSPARAMS2 0x8
#define XHCI_CAPREG_HCSPARAMS3 0xC
#define XHCI_CAPREG_HCCPARAMS1 0x10
#define XHCI_CAPREG_DBOFF 0x14
#define XHCI_CAPREG_RTSOFF 0x18
#define XHCI_CAPREG_HCCPARAMS2 0x1C

#define XHCI_OPREG_USBCMD 0x0
#define XHCI_OPREG_USBSTS 0x4
#define XHCI_OPREG_PAGESIZE 0x8
#define XHCI_OPREG_DNCTRL 0x14
#define XHCI_OPREG_CRCR 0x18
#define XHCI_OPREG_DCBAAP 0x30
#define XHCI_OPREG_CONFIG 0x38

#define XHCI_OPREG_PORTSCBASE 0x400
#define XHCI_OPREG_PORTPMSCBASE 0x404
#define XHCI_OPREG_PORTLIBASE 0x408
#define XHCI_OPREG_PORTHLPMCBASE 0x40C
#define XHCI_PORTOFFSET 0x10

#define XHCI_OPREG_PORTSC(x) \
(XHCI_OPREG_PORTSCBASE + (x-1) * XHCI_PORTOFFSET)
#define XHCI_OPREG_PORTPMSC(x) \
(XHCI_OPREG_PORTPMSCBASE + (x-1) * XHCI_PORTOFFSET)
#define XHCI_OPREG_PORTLI(x) \
(XHCI_OPREG_PORTLIBASE + (x-1) * XHCI_PORTOFFSET)
#define XHCI_OPREG_PORTHLPMC(x) \
(XHCI_OPREG_PORTHLPMCBASE + (x-1) * XHCI_PORTOFFSET)

#define XHCI_PORTSC_SPEED_GET(x) \
((x >> 10) & 0xF)

#define XHCI_RUNREG_MFINDEX 0x0
#define XHCI_RUNREG_IMANBASE 0x20
#define XHCI_RUNREG_IMODBASE 0x24
#define XHCI_RUNREG_ERSTSZBASE 0x28
#define XHCI_RUNREG_ERSTBABASE 0x30
#define XHCI_RUNREG_ERDPBASE 0x38
#define XHCI_INTERRUPTEROFFSET 0x20

#define XHCI_RUNREG_IMAN(x) \
(XHCI_RUNREG_IMANBASE + x*XHCI_INTERRUPTEROFFSET)
#define XHCI_RUNREG_IMOD(x) \
(XHCI_RUNREG_IMODBASE + x*XHCI_INTERRUPTEROFFSET)
#define XHCI_RUNREG_ERSTSZ(x) \
(XHCI_RUNREG_ERSTSZBASE + x*XHCI_INTERRUPTEROFFSET)
#define XHCI_RUNREG_ERSTBA(x) \
(XHCI_RUNREG_ERSTBABASE + x*XHCI_INTERRUPTEROFFSET)
#define XHCI_RUNREG_ERDP(x) \
(XHCI_RUNREG_ERDPBASE + x*XHCI_INTERRUPTEROFFSET)

#define XHCI_ECAP_LEGSUP 1
#define XHCI_ECAP_SUPPORT 2

#define XHCI_LEGCTLSTS_DISABLE_SMI	((0x7 << 1) + (0xff << 5) + (0x7 << 17))
#define XHCI_LEGCTLSTS_EVENTS_SMI	(0x7 << 29)

#define XHCI_TRB_TYPE_NORMAL 1
#define XHCI_TRB_TYPE_SETUP_STAGE 2
#define XHCI_TRB_TYPE_DATA_STAGE 3
#define XHCI_TRB_TYPE_STATUS_STAGE 4
#define XHCI_TRB_TYPE_LINK 6
#define XHCI_TRB_TYPE_EVENT_DATA_STAGE 7
#define XHCI_TRB_TYPE_NOOP 8
#define XHCI_TRB_TYPE_ENABLE_SLOT 9
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT 12
#define XHCI_TRB_TYPE_EVALUATE_CONTEXT 13
#define XHCI_TRB_TYPE_RESET_ENDPOINT 14
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32
#define XHCI_TRB_TYPE_COMMAND_COMPLETE 33
#define XHCI_TRB_TYPE_PORT_STATUS_CHANGE 34


#define XHCI_TRB_TYPE(x) ((uint64_t)x << 42)
#define XHCI_TRB_SLOTID(x) ((uint64_t)x << 56)

#define XHCI_GET_TRB_TYPE(x) \
((x >> 42) & 0x3F)

#define XHCI_TRB_ADDRESS_BSR ((uint64_t)1<<41)
#define XHCI_TRB_CONFIGUREEP_DECONF ((uint64_t)1<<41)
#define XHCI_TRB_ENABLED 0x100000000
#define XHCI_TRB_ENT 0x200000000
#define XHCI_TRB_TOGGLECYCLE 0x200000000
#define XHCI_TRB_ISP 0x400000000
#define XHCI_TRB_CN 0x1000000000
#define XHCI_TRB_IOC 0x2000000000
#define XHCI_TRB_IDT 0x4000000000
#define XHCI_TRB_TRT(x) ((uint64_t)x << 48)
#define XHCI_TRB_DIR_IN ((uint64_t)1 << 48)

#define XHCI_DOORBELL_HOST 0x0
#define XHCI_DOORBELL_HOST_COMMAND 0
#define XHCI_DOORBELL_ENPOINT0 1

#define XHCI_DOORBELL(slot) \
(XHCI_DOORBELL_HOST+slot*4)

#define XHCI_DOORBELL_ENDPOINT(epindex, streamid) ((streamid << 16) | epindex)

#define XHCI_COMPLETION_SUCCESS 1
#define XHCI_COMPLETION_STALL 6

template <class regt> class XhciRegister {
public:
	XhciRegister(void*& baseaddr, uint32_t registern, bool usebuffer = false)
		:m_baseaddr(baseaddr), m_register(registern), use_buffer(usebuffer)
	{
		m_buffered = false;
	}

	operator regt() {
		regt val;
		if (use_buffer)
		{
			if (!m_buffered)
			{
				val = *raw_offset<volatile regt*>(m_baseaddr, m_register);
				m_buffer = val;
				m_buffered = true;
			}
			else
				val = m_buffer;
		}
		else
			val = *raw_offset<volatile regt*>(m_baseaddr, m_register);
		return val;
	}
	regt operator = (regt i) 
	{ 
		if (!use_buffer)
		{
			*raw_offset<volatile regt*>(m_baseaddr, m_register) = i;
			return i;
		}
		else
		{
			m_buffer = i;
			return i;
		}
	}

	void write()
	{
		if (use_buffer)
		{
			*raw_offset<volatile regt*>(m_baseaddr, m_register) = m_buffer;
			m_buffered = false;
		}
	}

	regt operator = (XhciRegister rhs)
	{
		return (*this = (regt)rhs);
	}

	template <class T, uint16_t lsb, uint16_t msb, int16_t shift = 0> class RegisterField {
	public:
		RegisterField(XhciRegister<regt>& parent)
			:m_parent(parent)
		{

		}
		operator T() {
			regt regval = m_parent;
			return (regval & get_mask()) >> (lsb - shift);
		}
		T operator = (T val)
		{
			regt curreg = m_parent;
			regt value;
			if (shift < 0)
			{
				value = (regt)val << (lsb - shift);
			}
			else
			{
				value = ((regt)val >> shift) << (lsb);
			}
			regt mask = get_mask();
			if ((value & (~mask)) != 0)
			{
				//Error: outside allowable range
				return ~val;
			}
			curreg = (curreg & ~mask) | value;
			m_parent = curreg;
			return val;
		}
		T operator = (RegisterField rhs)
		{
			return (*this = (T)rhs);
		}
	private:
		regt get_mask() { return (((1ui64 << (msb - lsb + 1)) - 1) << lsb); }
		XhciRegister<regt>& m_parent;
	};

private:
	void*& m_baseaddr;
	const uint32_t m_register;
	regt m_buffer;
	const bool use_buffer;
	bool m_buffered;
};

class XhciCaplength : public XhciRegister<uint8_t> {
public:
	XhciCaplength(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_CAPREG_CAPLENGTH)
	{

	}
};

class XhciHciversion : public XhciRegister<uint16_t> {
public:
	XhciHciversion(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_CAPREG_HCIVERSION)
	{

	}
};

class XhciHcisparams1 : public XhciRegister<uint32_t> {
public:
	XhciHcisparams1(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_CAPREG_HCSPARAMS1),
		MaxSlots(*this), MaxIntrs(*this), MaxPorts(*this)
	{

	}
	RegisterField<uint8_t, 0, 7> MaxSlots;
	RegisterField<uint16_t, 8, 18> MaxIntrs;
	RegisterField<uint8_t, 24, 31> MaxPorts;
};

class XhciHcisparams2 : public XhciRegister<uint32_t> {
public:
	XhciHcisparams2(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_CAPREG_HCSPARAMS2),
		IST(*this), ERST_Max(*this), ETE(*this), MaxScratchHigh(*this), SPR(*this), MaxScratchLow(*this)
	{

	}
	RegisterField<uint8_t, 0, 3> IST;
	RegisterField<uint8_t, 4, 7> ERST_Max;
	RegisterField<uint8_t, 8, 8> ETE;
	RegisterField<uint8_t, 21, 25> MaxScratchHigh;
	RegisterField<uint8_t, 26, 26> SPR;
	RegisterField<uint8_t, 27, 31> MaxScratchLow;
};

class XhciHccparams1 : public XhciRegister<uint32_t> {
public:
	XhciHccparams1(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_CAPREG_HCCPARAMS1),
		AC64(*this),
		CSZ(*this),
		EcapPtr(*this)
	{

	}
	//TODO: Other fields
	RegisterField<uint8_t, 0, 0> AC64;
	RegisterField<uint8_t, 2, 2> CSZ;
	RegisterField<uint16_t, 16, 31> EcapPtr;
};

class XhciDboff : public XhciRegister<uint32_t> {
public:
	XhciDboff(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_CAPREG_DBOFF),
		Dboffset(*this)
	{

	}
	RegisterField<uint32_t, 2, 31, 2> Dboffset;
};
class XhciRuntimeoff : public XhciRegister<uint32_t> {
public:
	XhciRuntimeoff(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_CAPREG_RTSOFF),
		RuntimeOffset(*this)
	{

	}
	RegisterField<uint32_t, 5, 31, 5> RuntimeOffset;
};

class XhciRegisterBlock {
protected:
	XhciRegisterBlock(void*& baseaddr)
		:m_baseaddr(baseaddr)
	{

	}
	void*& m_baseaddr;
};
class XhciCapabilityRegisterBlock : public XhciRegisterBlock {
public:
	XhciCapabilityRegisterBlock(void*& baseaddr)
		:XhciRegisterBlock(baseaddr),
		CAPLENGTH(baseaddr),
		HCIVERSION(baseaddr),
		HCSPARAMS1(baseaddr),
		HCSPARAMS2(baseaddr),
		HCCPARAMS1(baseaddr),
		DBOFF(baseaddr),
		RUNTIMEOFF(baseaddr)
	{

	}
	XhciCaplength CAPLENGTH;
	XhciHciversion HCIVERSION;
	XhciHcisparams1 HCSPARAMS1;
	XhciHcisparams2 HCSPARAMS2;
	//TODO: HCS3
	XhciHccparams1 HCCPARAMS1;
	XhciDboff DBOFF;
	XhciRuntimeoff RUNTIMEOFF;
};

class XhciUsbcmd : public XhciRegister<uint32_t> {
public:
	XhciUsbcmd(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_OPREG_USBCMD),
		RunStop(*this),
		HcReset(*this),
		INTE(*this),
		HSEE(*this),
		LHCRST(*this),
		CSS(*this),
		CRS(*this),
		EWE(*this),
		EU3S(*this),
		CME(*this)
	{

	}
	RegisterField<uint8_t, 0, 0> RunStop;
	RegisterField<uint8_t, 1, 1> HcReset;
	RegisterField<uint8_t, 2, 2> INTE;
	RegisterField<uint8_t, 3, 3> HSEE;
	RegisterField<uint8_t, 7, 7> LHCRST;
	RegisterField<uint8_t, 8, 8> CSS;
	RegisterField<uint8_t, 9, 9> CRS;
	RegisterField<uint8_t, 10, 10> EWE;
	RegisterField<uint8_t, 11, 11> EU3S;
	RegisterField<uint8_t, 13, 13> CME;
};

class XhciUsbsts : public XhciRegister<uint32_t> {
public:
	XhciUsbsts(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_OPREG_USBSTS),
		HcHalted(*this),
		HSE(*this),
		EINT(*this),
		PCD(*this),
		SSS(*this),
		RSS(*this),
		SRE(*this),
		CNR(*this),
		HCE(*this)
	{

	}
	RegisterField<uint8_t, 0, 0> HcHalted;
	RegisterField<uint8_t, 2, 2> HSE;
	RegisterField<uint8_t, 3, 3> EINT;
	RegisterField<uint8_t, 4, 4> PCD;
	RegisterField<uint8_t, 8, 8> SSS;
	RegisterField<uint8_t, 9, 9> RSS;
	RegisterField<uint8_t, 10, 10> SRE;
	RegisterField<uint8_t, 11, 11> CNR;
	RegisterField<uint8_t, 13, 13> HCE;
};

class XhciPagesize : public XhciRegister<uint32_t> {
public:
	XhciPagesize(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_OPREG_PAGESIZE),
		PageSize(*this)
	{

	}
	RegisterField<uint16_t, 0, 15> PageSize;
};

class XhciCRCR : public XhciRegister<uint64_t> {
public:
	XhciCRCR(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_OPREG_CRCR),
		RCS(*this),
		CS(*this),
		CA(*this),
		CRR(*this),
		CommandRingPtr(*this)
	{

	}
	RegisterField<uint8_t, 0, 0> RCS;
	RegisterField<uint8_t, 1, 1> CS;
	RegisterField<uint8_t, 2, 2> CA;
	RegisterField<uint8_t, 3, 3> CRR;
	RegisterField<uint64_t, 6, 63, 6> CommandRingPtr;
};

class XhciDCBAAP : public XhciRegister<uint64_t> {
public:
	XhciDCBAAP(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_OPREG_DCBAAP),
		DeviceContextPtr(*this)
	{

	}
	RegisterField<uint64_t, 6, 63, 6> DeviceContextPtr;
};

class XhciConfig : public XhciRegister<uint32_t> {
public:
	XhciConfig(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_OPREG_CONFIG),
		EnabledDeviceSlots(*this),
		U3E(*this),
		CIE(*this)
	{

	}
	RegisterField<uint8_t, 0, 7> EnabledDeviceSlots;
	RegisterField<uint8_t, 8, 8> U3E;
	RegisterField<uint8_t, 9, 9> CIE;
};

class XhciPortStatus : public XhciRegister<uint32_t> {
public:
	XhciPortStatus(void*& baseaddr, uint32_t port)
		:XhciRegister(baseaddr, XHCI_OPREG_PORTSC(port)),
		CCS(*this),
		PED(*this),
		OCA(*this),
		PortReset(*this),
		PLS(*this),
		PP(*this),
		PortSpeed(*this),
		PIC(*this),
		LWS(*this),
		CSC(*this),
		PEC(*this),
		WRC(*this),
		OCC(*this),
		PRC(*this)
	{
	}
	RegisterField<const uint8_t, 0, 0> CCS;
	RegisterField<uint8_t, 1, 1> PED;
	RegisterField<const uint8_t, 3, 3> OCA;
	RegisterField<uint8_t, 4, 4> PortReset;
	RegisterField<uint8_t, 5, 8> PLS;
	RegisterField<uint8_t, 9, 9> PP;
	RegisterField<uint8_t, 10, 13> PortSpeed;
	RegisterField<uint8_t, 14, 15> PIC;
	RegisterField<uint8_t, 16, 16> LWS;
	RegisterField<uint8_t, 17, 17> CSC;
	RegisterField<uint8_t, 18, 18> PEC;
	RegisterField<uint8_t, 19, 19> WRC;
	RegisterField<uint8_t, 20, 20> OCC;
	RegisterField<uint8_t, 21, 21> PRC;
	//TODO: Other fields
};
class XhciPortRegisterBlock : public XhciRegisterBlock {
public:
	XhciPortRegisterBlock(void*& oppbase, uint32_t portNum)
		:XhciRegisterBlock(oppbase),
		PORTSC(oppbase, portNum)
	{
	}
	XhciPortStatus PORTSC;
	//TODO: Other port registers
};

class XhciOperationalRegisterBlock : public XhciRegisterBlock {
public:
	XhciOperationalRegisterBlock(void*& baseaddr)
		:XhciRegisterBlock(baseaddr),
		USBCMD(baseaddr),
		USBSTS(baseaddr),
		CRCR(baseaddr),
		DCBAAP(baseaddr),
		CONFIG(baseaddr)
	{

	}
	XhciUsbcmd USBCMD;
	XhciUsbsts USBSTS;
	//TODO: DNCTRL
	XhciCRCR CRCR;
	XhciDCBAAP DCBAAP;
	XhciConfig CONFIG;
	XhciPortRegisterBlock Port(uint32_t portNum)
	{
		return XhciPortRegisterBlock(m_baseaddr, portNum);
	}
};

class XhciMfindex : public XhciRegister<uint32_t> {
public:
	XhciMfindex(void*& baseaddr)
		:XhciRegister(baseaddr, XHCI_RUNREG_MFINDEX),
		MicroframeIndex(*this)
	{

	}
	RegisterField<uint16_t, 0, 13> MicroframeIndex;
};

class XhciInterrupterIman : public XhciRegister<uint32_t> {
public:
	XhciInterrupterIman(void*& baseaddr, uint32_t interrupter)
		:XhciRegister(baseaddr, XHCI_RUNREG_IMAN(interrupter)),
		InterruptPending(*this),
		InterruptEnable(*this)
	{
	}
	RegisterField<uint8_t, 0, 0> InterruptPending;
	RegisterField<uint8_t, 1, 1> InterruptEnable;
};
class XhciInterrupterImod : public XhciRegister<uint32_t> {
public:
	XhciInterrupterImod(void*& baseaddr, uint32_t interrupter)
		:XhciRegister(baseaddr, XHCI_RUNREG_IMOD(interrupter), true),
		InterruptInterval(*this),
		InterruptCounter(*this)
	{
	}
	RegisterField<uint16_t, 0, 15> InterruptInterval;
	RegisterField<uint16_t, 16, 31> InterruptCounter;
};
class XhciInterrupterErstsz : public XhciRegister<uint32_t> {
public:
	XhciInterrupterErstsz(void*& baseaddr, uint32_t interrupter)
		:XhciRegister(baseaddr, XHCI_RUNREG_ERSTSZ(interrupter)),
		Size(*this)
	{
	}
	RegisterField<uint16_t, 0, 15> Size;
};
class XhciInterrupterErstba : public XhciRegister<uint64_t> {
public:
	XhciInterrupterErstba(void*& baseaddr, uint32_t interrupter)
		:XhciRegister(baseaddr, XHCI_RUNREG_ERSTBA(interrupter)),
		SegTableBase(*this)
	{
	}
	RegisterField<uint64_t, 6, 63, 6> SegTableBase;
};
class XhciInterrupterErdp : public XhciRegister<uint64_t> {
public:
	XhciInterrupterErdp(void*& baseaddr, uint32_t interrupter)
		:XhciRegister(baseaddr, XHCI_RUNREG_ERDP(interrupter)),
		SegmentIndex(*this),
		HandlerBusy(*this),
		DequeuePtr(*this)
	{
	}
	void update(paddr_t dp, bool busy)
	{
		uint64_t regval = *this;
		//Special logic for keeping busy correct
		regval &= 0x0F;
		regval |= dp;
		if (busy)
			regval &= (UINT64_MAX - (1 << 3));
		else
			regval |= (1 << 3);
		(XhciRegister<uint64_t>&)*this = regval;
	}
	RegisterField<uint8_t, 0, 2> SegmentIndex;
	RegisterField<uint8_t, 3, 3> HandlerBusy;
	RegisterField<uint64_t, 4, 63, 4> DequeuePtr;
};
class XhciInterrupterRegisterBlock : public XhciRegisterBlock {
public:
	XhciInterrupterRegisterBlock(void*& oppbase, uint32_t interrupter)
		:XhciRegisterBlock(oppbase),
		IMAN(oppbase, interrupter),
		IMOD(oppbase, interrupter),
		ERSTSZ(oppbase, interrupter),
		ERSTBA(oppbase, interrupter),
		ERDP(oppbase, interrupter)
	{
	}
	XhciInterrupterIman IMAN;
	XhciInterrupterImod IMOD;
	XhciInterrupterErstsz ERSTSZ;
	XhciInterrupterErstba ERSTBA;
	XhciInterrupterErdp ERDP;
};

class XhciRuntimeRegisterBlock : public XhciRegisterBlock {
public:
	XhciRuntimeRegisterBlock(void*& baseaddr)
		:XhciRegisterBlock(baseaddr),
		MFINDEX(baseaddr)
	{

	}
	XhciMfindex MFINDEX;
	XhciInterrupterRegisterBlock Interrupter(uint32_t interrupter)
	{
		return XhciInterrupterRegisterBlock(m_baseaddr, interrupter);
	}
};

#pragma pack(push, 1)
struct xhci_trb {
	uint64_t lowval;
	uint64_t highval;
};

struct xhci_command : public xhci_trb {
};
#pragma pack(pop)

static xhci_command* create_address_command(bool bsr, paddr_t context, uint16_t slot)
{
	xhci_command* ret = new xhci_command;
	uint64_t lowval = 0, highval = 0;
	lowval = context;
	highval = XHCI_TRB_ENABLED | XHCI_TRB_TYPE(XHCI_TRB_TYPE_ADDRESS_DEVICE) | XHCI_TRB_SLOTID(slot);
	highval |= bsr ? XHCI_TRB_ADDRESS_BSR : 0;

	ret->lowval = lowval;
	ret->highval = highval;
	return ret;
}

static xhci_command* create_configureendpoint_command(paddr_t context, uint16_t slot, bool deconfigure)
{
	xhci_command* ret = new xhci_command;
	uint64_t lowval = 0, highval = 0;
	lowval = context;
	highval = XHCI_TRB_ENABLED | XHCI_TRB_TYPE(XHCI_TRB_TYPE_CONFIGURE_ENDPOINT) | XHCI_TRB_SLOTID(slot);
	highval |= deconfigure ? XHCI_TRB_CONFIGUREEP_DECONF : 0;

	ret->lowval = lowval;
	ret->highval = highval;
	return ret;
}

static xhci_command* create_resetendpoint_command(uint16_t slot, uint16_t endpoint)
{
	xhci_command* ret = new xhci_command;
	uint64_t lowval = 0, highval = 0;
	highval = XHCI_TRB_ENABLED | XHCI_TRB_TYPE(XHCI_TRB_TYPE_RESET_ENDPOINT) | XHCI_TRB_SLOTID(slot) | ((uint64_t)endpoint << 16);

	ret->lowval = lowval;
	ret->highval = highval;
	return ret;
}

static xhci_command* create_evaluate_command(paddr_t context, uint16_t slot)
{
	xhci_command* ret = new xhci_command;
	uint64_t lowval = 0, highval = 0;
	lowval = context;
	highval = XHCI_TRB_TYPE(XHCI_TRB_TYPE_EVALUATE_CONTEXT) | XHCI_TRB_SLOTID(slot);

	ret->lowval = lowval;
	ret->highval = highval;
	return ret;
}

static xhci_command* create_enableslot_command(uint8_t slottype)
{
	xhci_command* ret = new xhci_command;
	ret->lowval = 0;
	ret->highval = XHCI_TRB_TYPE(XHCI_TRB_TYPE_ENABLE_SLOT) | ((uint64_t)slottype << 16);
	return ret;
}

static xhci_command* create_setup_stage_trb(uint8_t rType, uint8_t bRequest, uint16_t value, uint16_t wIndex, uint16_t wLength, uint8_t trt)
{
	xhci_command* ret = new xhci_command;
	ret->lowval = rType | (bRequest << 8) | (value << 16) | ((uint64_t)wIndex << 32) | ((uint64_t)wLength << 48);
	ret->highval = 8 | XHCI_TRB_IDT | XHCI_TRB_TRT(trt) | XHCI_TRB_TYPE(XHCI_TRB_TYPE_SETUP_STAGE);
	return ret;
}

static xhci_command* create_data_stage_trb(paddr_t buffer, uint16_t size, bool indirection, bool chain = false)
{
	xhci_command* ret = new xhci_command;
	ret->lowval = buffer;
	ret->highval = XHCI_TRB_ENT | (indirection ? XHCI_TRB_DIR_IN : 0) | XHCI_TRB_TYPE(XHCI_TRB_TYPE_DATA_STAGE) |(chain ? XHCI_TRB_CN : 0) | size;
	return ret;
}

static xhci_command* create_event_data_trb(paddr_t buffer)
{
	xhci_command* ret = new xhci_command;
	ret->lowval = buffer;
	ret->highval = XHCI_TRB_IOC | XHCI_TRB_TYPE(XHCI_TRB_TYPE_EVENT_DATA_STAGE);
	return ret;
}

static xhci_command* create_status_stage_trb(bool indirection)
{
	xhci_command* ret = new xhci_command;
	ret->lowval = 0;
	ret->highval = XHCI_TRB_ENT | (indirection ? 0 : XHCI_TRB_DIR_IN) | XHCI_TRB_TYPE(XHCI_TRB_TYPE_STATUS_STAGE) | XHCI_TRB_IOC;
	return ret;
}

static xhci_trb* create_link_trb(paddr_t nextSegment, bool togglecycle)
{
	xhci_trb* ret = new xhci_trb;
	ret->lowval = nextSegment;
	ret->highval = XHCI_TRB_TYPE(XHCI_TRB_TYPE_LINK) | (togglecycle ? XHCI_TRB_TOGGLECYCLE : 0);
	return ret;
}

static xhci_trb* create_normal_trb(paddr_t dataBuffer, uint32_t transferLength, uint8_t TdSize, uint16_t InterrupterTarget, bool chain)
{
	xhci_trb* ret = new xhci_trb;
	ret->lowval = dataBuffer;
	ret->highval = ((InterrupterTarget & 0x3FF) << 22) | ((TdSize & 0x1F) << 17) | (transferLength & 0x1FFFF) | \
		(chain ? XHCI_TRB_CN : 0) | (!chain ? XHCI_TRB_IOC : 0) | XHCI_TRB_TYPE(XHCI_TRB_TYPE_NORMAL);
	return ret;
}

#endif

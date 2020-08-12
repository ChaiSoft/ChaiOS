#ifndef CHAIOS_NIC_H
#define CHAIOS_NIC_H

struct nic_info {
	void(*send_func)(void* packet, void* paramblock);
	void* paramblock;
};

#endif

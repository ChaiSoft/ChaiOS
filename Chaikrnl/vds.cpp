#include "vds.h"
#include <arch/cpu.h>
#include <redblack.h>
#include <linkedlist.h>
#include <spinlock.h>
#include <kstdio.h>
#include <string.h>
#include <endian.h>
#include <guid.h>

static size_t handle_alloc = 1;

struct internal_disk_info {
	PCHAIOS_VDS_DISK publicinfo;
	bool isbound;
};

struct interested_filesystems {
	chaios_vds_enum_callback callback;
	linked_list_node<interested_filesystems*> llnode;
};

static RedBlackTree<HDISK, internal_disk_info*> handle_translator;
static spinlock_t treelock;
static LinkedList<interested_filesystems*> interestedFilesystems;

static linked_list_node <interested_filesystems*>& get_node(interested_filesystems* node)
{
	return node->llnode;
}


#pragma pack(push, 1)
typedef struct _gpt_partition_header {
	uint64_t signature;		//"EFI PART"
	uint32_t revision;
	uint32_le headerSize;
	uint32_le crc;
	uint32_t reserved;
	uint64_t headerLba;
	uint64_t mirrorHeaderLba;
	uint64_t firstUsableBlock;
	uint64_t lastUsableBlock;
	GUID diskGuid;
	uint64_t entriesStartingLba;
	uint32_t numberPartitionEntries;
	uint32_t partitionEntrySize;
	uint32_t partititionArrayCrc;
}GPT_PARTITION_HEADER, *PGPT_PARTITION_HEADER;
static_assert(sizeof(GPT_PARTITION_HEADER) == 0x5C, "VDS: packing error");

typedef struct _gpt_partition_entry {
	GUID partitionType;
	GUID partitionId;
	uint64_t startLba;
	uint64_t endLba;
	uint64_t attributes;
	char16_t partitionName[36];
}GPT_PARTITION_ENTRY, *PGPT_PARTITION_ENTRY;
static_assert(sizeof(GPT_PARTITION_ENTRY) == 0x80, "VDS: packing error");

typedef struct _mbr_partition_entry {
	uint8_t attributes;	//0x80: bootable
	uint8_t startHead;
	uint16_le startCylSect;
	uint8_t systemId;
	uint8_t endHead;
	uint16_le endCylSect;
	uint32_le startLba;
	uint32_le partitionSize;
}MBR_PARTITION_ENTRY, *PMBR_PARTITION_ENTRY;

typedef struct _mbr_partition_entry48 {
	uint8_t attributes;	//Bit 0 set: 48 bit LBA
	uint8_t sig1;		//0x14
	uint16_le startLbaHigh;
	uint8_t systemId;
	uint8_t sig2;		//0xEB
	uint16_le partSizeHigh;
	uint32_le startLba;
	uint32_le partitionSize;
}MBR_PARTITION_ENTRY48, *PMBR_PARTITION_ENTRY48;

typedef struct _mbr_bootsector {
	uint8_t bootstrap[440];
	uint32_le diskSignature;
	uint16_t reserved;
	MBR_PARTITION_ENTRY entries[4];
	uint16_le bootSig;
}MBR_BOOTSECTOR, *PMBR_BOOTSECTOR;
static_assert(sizeof(MBR_BOOTSECTOR) == 512, "VDS: packing error");

#pragma pack(pop)

static GUID empty_gpt = NULL_GUID;

static bool inited = false;

static vds_err_t partition_disk_read(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent);
static vds_err_t partition_disk_write(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent);
static PCHAIOS_VDS_PARAMS partition_disk_params(void* param);

class CPartition {
protected:
	virtual PCHAIOS_VDS_PARAMS params() = 0;
	virtual vds_err_t read(lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent) = 0;
	virtual vds_err_t write(lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent) = 0;

	friend vds_err_t partition_disk_read(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent);
	friend vds_err_t partition_disk_write(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent);
	friend PCHAIOS_VDS_PARAMS partition_disk_params(void* param);
};

class CGptPartition : public CPartition {
public:
	CGptPartition(HDISK parent, PGPT_PARTITION_ENTRY partentry, PCHAIOS_VDS_PARAMS paramparent)
		:m_startLba(partentry->startLba), m_endLba(partentry->endLba), m_parent(parent)
	{
		m_disk.fn_param = this;
		m_disk.read_fn = &partition_disk_read;
		m_disk.write_fn = &partition_disk_write;
		m_disk.get_params = &partition_disk_params;

		m_params.parent = parent;
		m_params.diskType = partentry->partitionType;
		m_params.diskId = partentry->partitionId;
		m_params.sectorSize = paramparent->sectorSize;
		m_params.diskSize = 1 + m_endLba - m_startLba;

		RegisterVdsDisk(&m_disk);
	};
protected:
	virtual PCHAIOS_VDS_PARAMS params()
	{
		return &m_params;
	}
	virtual vds_err_t read(lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent)
	{
		if (m_startLba + blockaddr + count - 1 > m_endLba)
			return -1;
		return ReadVdsDisk(m_parent, blockaddr + m_startLba, count, buffer, completionEvent);
	}
	virtual vds_err_t write(lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent)
	{
		if (m_startLba + blockaddr + count - 1 > m_endLba)
			return -1;
		return WriteVdsDisk(m_parent, blockaddr + m_startLba, count, buffer, completionEvent);
	}

private:
	const HDISK m_parent;
	const lba_t m_startLba;
	const lba_t m_endLba;
	CHAIOS_VDS_DISK m_disk;
	CHAIOS_VDS_PARAMS m_params;
};

class CMbrPartition : public CPartition {
public:
	CMbrPartition(HDISK parent, PMBR_BOOTSECTOR bootsector, uint8_t partIndex, PCHAIOS_VDS_PARAMS paramparent)
		:m_parent(parent)
	{
		m_disk.fn_param = this;
		m_disk.read_fn = &partition_disk_read;
		m_disk.write_fn = &partition_disk_write;
		m_disk.get_params = &partition_disk_params;

		PMBR_PARTITION_ENTRY partentry = &bootsector->entries[partIndex];

		//Deal with both standard MBR format, and unofficial 48 bit
		bool is48bit = false;
		PMBR_PARTITION_ENTRY48 entry48 = (PMBR_PARTITION_ENTRY48)partentry;

		uint8_t base_attr = partentry->attributes & 0x7F;
		if (base_attr != 0)
		{
			if (base_attr == 0x1)
			{
				if (entry48->sig1 != 0x14 || entry48->sig2 != 0xEB)
				{
					delete this;		//Invalid
					return;
				}
				//All signatures check out
				is48bit = true;
			}
			else
			{
				//Invalid
				delete this;
				return;
			}
		}

		m_startLba = partentry->startLba | (is48bit ? ((lba_t)entry48->startLbaHigh << 32) : 0);
		m_diskSize = partentry->partitionSize | (is48bit ? ((lba_t)entry48->partSizeHigh << 32) : 0);

		m_params.parent = parent;
		m_params.diskType = CHAIOS_VDS_DISK_MBR(partentry->systemId);
		m_params.diskId = MKGUID(bootsector->diskSignature, partentry->startLba & UINT16_MAX, partentry->startLba >> 16, partentry->partitionSize & UINT16_MAX, partentry->systemId, partentry->startHead, 0, 0, 0, 0);
		m_params.sectorSize = paramparent->sectorSize;
		m_params.diskSize = m_diskSize;

		RegisterVdsDisk(&m_disk);
	};
protected:
	virtual PCHAIOS_VDS_PARAMS params()
	{
		return &m_params;
	}
	virtual vds_err_t read(lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent)
	{
		if (blockaddr + count > m_diskSize)
			return -1;
		return ReadVdsDisk(m_parent, blockaddr + m_startLba, count, buffer, completionEvent);
	}
	virtual vds_err_t write(lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent)
	{
		if (blockaddr + count > m_diskSize)
			return -1;
		return WriteVdsDisk(m_parent, blockaddr + m_startLba, count, buffer, completionEvent);
	}

private:
	const HDISK m_parent;
	lba_t m_startLba;
	lba_t m_diskSize;
	CHAIOS_VDS_DISK m_disk;
	CHAIOS_VDS_PARAMS m_params;
};

static vds_err_t partition_disk_read(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent)
{
	CPartition* part = (CPartition*)param;
	return part->read(blockaddr, count, buffer, completionEvent);
}
static vds_err_t partition_disk_write(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent)
{
	CPartition* part = (CPartition*)param;
	return part->write(blockaddr, count, buffer, completionEvent);
}
static PCHAIOS_VDS_PARAMS partition_disk_params(void* param)
{
	CPartition* part = (CPartition*)param;
	return part->params();
}

static vds_enum_result partitionManagerCallback(HDISK disk)
{
	//kprintf(u"Checking for partitions on disk %d\n", disk);

	auto params = GetVdsParams(disk);

	auto sectorSize = params->sectorSize;

	void* sector = new uint8_t[sectorSize];

	vds_enum_result result = RESULT_NOTBOUND;
	//GPT check
	if (ReadVdsDisk(disk, 1, 1, sector, nullptr) != 0)
		goto ret_result;		//Could not read!
	if (strncmp((char*)sector, "EFI PART", 8) == 0)
	{
		//GPT found!
		//kprintf(u" Disk %d is GPT formatted\n", disk);
		PGPT_PARTITION_HEADER header = (PGPT_PARTITION_HEADER)sector;
		uint64_t startlba = header->entriesStartingLba;
		uint32_t count = header->numberPartitionEntries;
		uint32_t entrySize = header->partitionEntrySize;

		uint64_t curlba = 1;
		for (uint_fast32_t i = 0; i < count; ++i)
		{
			uint64_t lba = startlba + (i * entrySize) / sectorSize;
			uint64_t offset = (i * entrySize) % sectorSize;
			if (lba != curlba)
			{
				if (ReadVdsDisk(disk, lba, 1, sector, nullptr) != 0)
					return result;
				curlba = lba;
			}
			//Read partition entry
			PGPT_PARTITION_ENTRY entry = raw_offset<PGPT_PARTITION_ENTRY>(sector, offset);
			if (entry->partitionType == empty_gpt)
				continue;
			//kprintf(u"Found partition %s: %d->%d\n", entry->partitionName, entry->startLba, entry->endLba);

			CGptPartition* partition = new CGptPartition(disk, entry, params);
			if (partition)
				result = RESULT_NEWDISKS;

		}
	}
	else
	{
		//Check for BIOS MBR
		if (ReadVdsDisk(disk, 0, 1, sector, nullptr) != 0)
			goto ret_result;		//Could not read!

		PMBR_BOOTSECTOR mbr = (PMBR_BOOTSECTOR)sector;
		if (params->parent == NULL)		//Raw disk
		{
			if (mbr->bootSig != 0xAA55)
				goto ret_result;
		}
		else
		{
			//Extended partition support, we reside in a partition
			GUID mbrextid = CHAIOS_VDS_DISK_MBR(0x0F);
			GUID mbrextidchs = CHAIOS_VDS_DISK_MBR(0x05);
			if (params->diskType != mbrextid && params->diskType != mbrextidchs)
				goto ret_result;
			//Extended partition!

		}
		for (uint_fast8_t i = 0; i < 4; ++i)
		{
			if (mbr->entries[i].systemId == 0)
				continue;
			//Partition entry
			CMbrPartition* partition = new CMbrPartition(disk, mbr, i, params);
			if (partition)
				result = RESULT_NEWDISKS;
		}

	}
ret_result:
	delete[] sector;
	return result;
}

void init_vds()
{
	if (inited)
		return;
	inited = true;
	treelock = create_spinlock();

	interestedFilesystems.init(get_node);
	VdsRegisterFilesystem(partitionManagerCallback);
}

EXTERN CHAIKRNL_FUNC HDISK RegisterVdsDisk(PCHAIOS_VDS_DISK diskInfo)
{
	size_t alloc = handle_alloc;
	while (!arch_cas(&handle_alloc, alloc, alloc + 1))
		alloc = handle_alloc;
	
	HDISK handle = (HDISK)alloc;
	internal_disk_info* intinfo = new internal_disk_info;
	if (!intinfo)
		return NULL;
	intinfo->publicinfo = diskInfo;
	intinfo->isbound = false;
	auto st = acquire_spinlock(treelock);
	handle_translator[handle] = intinfo;
	release_spinlock(treelock, st);

	return handle;
}
EXTERN CHAIKRNL_FUNC vds_err_t ReadVdsDisk(HDISK disk, lba_t block, vds_length_t count, void* buffer, semaphore_t* completionEvent)
{
	auto st = acquire_spinlock(treelock);
	auto it = handle_translator.find(disk);
	release_spinlock(treelock, st);
	if (it == handle_translator.end())
		return -1;
	PCHAIOS_VDS_DISK diskinf = it->second->publicinfo;
	if (!diskinf->read_fn)
		return -1;
	return diskinf->read_fn(diskinf->fn_param, block, count, buffer, completionEvent);
}
EXTERN CHAIKRNL_FUNC vds_err_t WriteVdsDisk(HDISK disk, lba_t block, vds_length_t count, void* buffer, semaphore_t* completionEvent)
{
	return -1;
}
EXTERN CHAIKRNL_FUNC PCHAIOS_VDS_PARAMS GetVdsParams(HDISK disk)
{
	auto st = acquire_spinlock(treelock);
	auto it = handle_translator.find(disk);
	release_spinlock(treelock, st);
	if (it == handle_translator.end())
		return nullptr;
	PCHAIOS_VDS_DISK diskinf = it->second->publicinfo;
	return diskinf->get_params(diskinf->fn_param);
}

EXTERN CHAIKRNL_FUNC void enumerate_disks(chaios_vds_enum_callback callback)
{
	//auto st = acquire_spinlock(treelock);
	auto it = handle_translator.begin();
	while (it != handle_translator.end())
	{
		callback(it->first);
		++it;
	}
	//release_spinlock(treelock, st);
}

EXTERN CHAIKRNL_FUNC void VdsRegisterFilesystem(chaios_vds_enum_callback callback)
{
	interested_filesystems* ifs = new interested_filesystems;
	ifs->callback = callback;
	interestedFilesystems.insert(ifs);
}

void vds_start_filesystem_matching()
{
	bool newdisks;
	do {
		newdisks = false;
		for (auto diskit = handle_translator.begin(); diskit != handle_translator.end(); ++diskit)
		{
			internal_disk_info* diskdata = diskit->second;
			if (diskdata->isbound)
				continue;

			bool bound = false;

			for (auto it = interestedFilesystems.begin(); it != interestedFilesystems.end(); ++it)
			{
				const chaios_vds_enum_callback& fsdrv = (*it)->callback;
				VDS_ENUM_RESULT result = (VDS_ENUM_RESULT)fsdrv(diskit->first);
				switch (result)
				{
				case RESULT_NEWDISKS:
					newdisks = true;
				case RESULT_BOUND:
					bound = true;
					break;
				case RESULT_NOTBOUND:
				default:
					break;
				}
				if (bound)		//No need for further FS checks
					break;
			}
			diskdata->isbound = bound;
			if (newdisks)		//Can't keep iterating now the tree is updated?
				break;
		}
	} while (newdisks);
}
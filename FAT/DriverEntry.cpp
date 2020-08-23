#include <chaikrnl.h>
#include <kstdio.h>
#include <string.h>
#include <endian.h>

#include <vds.h>

#pragma pack(push, 1)
typedef struct _fat_bpb {
	uint8_t jmp[3];
	char oemid[8];
	uint16_le bytesPerSector;
	uint8_t sectorsPerCluster;
	uint16_le reservedSectors;
	uint8_t numFats;
	uint16_le numDirectoryEntries;
	uint16_le totalSectorsShort;
	uint8_t mediaDescriptor;
	uint16_le sectorsPerFat;
	uint16_le sectorsPerTrack;
	uint16_le heads;
	uint32_le hiddenSectors;
	uint32_le largeSectorCount;
	union {
		struct {
			uint8_t driveNumber;
			uint8_t flagsWinNT;
			uint8_t signature;
			uint32_le volumeSerialNumber;
			char volumeLabel[11];
			char systemIdentifier[8];
		}FAT16;

		struct {
			uint32_le sectorsPerFat;
			uint16_le flags;
			uint16_le fatVersion;
			uint32_le rootDirectoryCluster;
			uint16_le fsInfoSector;
			uint16_le backupBootSector;
			uint8_t reserved[12];
			uint8_t driveNumber;
			uint8_t flagsWinNT;
			uint8_t signature;
			uint32_le volumeSerialNumber;
			char volumeLabel[11];
			char systemIdentifier[8];
		}FAT32;
	};
}FAT_BPB, *PFAT_BPB;
#pragma pack(pop)

static GUID fat_guids[] =
{
	CHAIOS_VDS_DISK_MBR(0x01),
	CHAIOS_VDS_DISK_MBR(0x04),
	CHAIOS_VDS_DISK_MBR(0x06),
	CHAIOS_VDS_DISK_MBR(0x08),
	CHAIOS_VDS_DISK_MBR(0x0B),
	CHAIOS_VDS_DISK_MBR(0x0C),
	CHAIOS_VDS_DISK_MBR(0x0E),
	MKGUID(0xC12A7328, 0xF81F, 0x11D2, 0xBA4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B),		//EFI System Partition
	MKGUID(0xEBD0A0A2, 0xB9E5, 0x4433, 0x87C0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7),		//Microsoft basic data partition
	NULL_GUID
};

static bool match_fat_guid(GUID& guid)
{
	GUID nullguid = NULL_GUID;
	for (unsigned i = 0; fat_guids[i] != nullguid; ++i)
	{
		if (fat_guids[i] == guid)
			return true;
	}
	return false;
}

static vds_enum_result FatVdsCallback(HDISK disk)
{
	auto params = GetVdsParams(disk);
	if (params->parent != NULL)
	{
		//Check partition type ID
		if (!match_fat_guid(params->diskType))
		{
			return RESULT_NOTBOUND;
		}
	}
	//Superfloppy or matching partition ID... is it FAT?
	void* sector = new uint8_t[params->sectorSize];
	if (ReadVdsDisk(disk, 0, 1, sector, nullptr) != 0)
		return RESULT_NOTBOUND;

	PFAT_BPB bpb = (PFAT_BPB)sector;

	switch (bpb->sectorsPerCluster)
	{
	case 1:
	case 2:
	case 4:
	case 8:
	case 16:
	case 32:
	case 64:
	case 128:
		break;
	default:
		//Not FAT
		return RESULT_NOTBOUND;
	}

	switch (bpb->bytesPerSector)
	{
	case 128:
	case 256:
	case 512:
	case 1024:
	case 2048:
	case 4096:
	case 8192:
	case 16536:
		break;
	default:
		//Not FAT
		return RESULT_NOTBOUND;
	}

	if (bpb->numFats == 0)
		return RESULT_NOTBOUND;

	if(bpb->reservedSectors == 0)
		return RESULT_NOTBOUND;

	if(bpb->totalSectorsShort == 0 && bpb->largeSectorCount == 0)
		return RESULT_NOTBOUND;

	kprintf(u"FAT partition on disk %d\n", disk);

	return RESULT_BOUND;
}


EXTERN int CppDriverEntry(void* param)
{
	//Register with VDS
	VdsRegisterFilesystem(FatVdsCallback);
	return 0;
}
#include <chaikrnl.h>
#include <kstdio.h>
#include <string.h>
#include <endian.h>

#include <vds.h>
#include <vfs.h>

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

struct _tag_file {
	const char16_t* filename;
	bool isDirectory;
	uint64_t startCluster;
	uint32_t vfsAttributes;
};

pfile fat_open(void* fsObject, pfile directory, const char16_t* name, uint32_t mode, semaphore_t* asyncSem);
void fat_close(void* fsObject, pfile file);
ssize_t fat_read(void* fsObject, pfile file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem);
ssize_t fat_write(void* fsObject, pfile file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem);
uint32_t fat_attributes(void* fsObject, pfile file);

static bool print_callback(const char16_t* filename, const void* noParam)
{
	kprintf(u"  %s\n", filename);
	return false;
}
static const char16_t* testFilename = u"EFI";
static const char16_t* testFilename2 = u"ChaiOS";
static bool find_by_name(const char16_t* filename, size_t index, const void* param)
{
	const char16_t* nameToMatch = (const char16_t*)param;
	unsigned i = 0;
	for (; nameToMatch[i] && filename[i]; ++i)
		if (nameToMatch[i] != filename[i])
			return false;
	return (nameToMatch[i] == filename[i]);		//Both NULL
}

static bool find_by_index(const char16_t* filename, size_t index, const void* param)
{
	size_t target = (size_t)param;
	return (index == target);
}

class CFatVolume {
public:
	CFatVolume(HDISK disk, PFAT_BPB bpb)
		:m_disk(disk), m_bpb(bpb)
	{
		m_SectorSize = VdsGetParams(disk)->sectorSize;
	}
	void init()
	{
		m_driverinfo.fsObject = this;
		m_driverinfo.read = fat_read;
		m_driverinfo.write = fat_write;
		m_driverinfo.open = fat_open;
		m_driverinfo.close = fat_close;
		m_driverinfo.getAttributes = fat_attributes;
		//Work out parameters from BPB

		m_SectorsPerCluster = m_bpb->sectorsPerCluster;
		size_t totalSectors = (m_bpb->totalSectorsShort == 0) ? m_bpb->largeSectorCount : m_bpb->totalSectorsShort;
		size_t fatSize = (m_bpb->sectorsPerFat == 0) ? m_bpb->FAT32.sectorsPerFat : m_bpb->sectorsPerFat;
		//Not valid for FAT32
		m_rootDirSectors = ((m_bpb->numDirectoryEntries * 32) + m_bpb->bytesPerSector - 1) / m_bpb->bytesPerSector;
		size_t dataSectors = totalSectors - (m_bpb->reservedSectors + m_bpb->numFats * fatSize + m_rootDirSectors);
		size_t totalClusters = dataSectors / m_bpb->sectorsPerCluster;
		m_firstDataSector = m_bpb->reservedSectors + m_bpb->numFats * fatSize + m_rootDirSectors;

		m_FirstFatSector = m_bpb->reservedSectors;

		if (dataSectors < 4085)
			m_fatVer = FAT12;
		else if (dataSectors < 65525)
			m_fatVer = FAT16;
		else if (totalClusters < 268435445)
			m_fatVer = FAT32;

		if (m_fatVer != FAT32)
			m_FirstRootDirSector = m_firstDataSector - m_rootDirSectors;
		else
			m_FirstRootDirSector = m_bpb->FAT32.rootDirectoryCluster;
#if 0
		kprintf(u"FAT partition on disk %d, type: FAT%d\n", m_disk, readableFat(m_fatVer));
		searchDirectory(nullptr, print_callback , nullptr);
		//readRootDirectory();
		pfile testDir = searchDirectory(nullptr, find_by_name, testFilename);
		if (testDir)
		{
			kprintf(u"Directory of %s:\n", testFilename);
			searchDirectory(testDir, print_callback, nullptr);
			testDir = searchDirectory(testDir, find_by_name, testFilename2);
			if (testDir)
			{
				kprintf(u"Directory of %s:\n", testFilename2);
				searchDirectory(testDir, print_callback, nullptr);
			}
		}
#endif		
		VfsRegisterFilesystem(&m_driverinfo);
	}
protected:

	typedef bool(*dir_search_callback)(const char16_t* filename, size_t index, const void* param);

	vds_err_t ReadCluster(uint64_t cluster, void* buffer, semaphore_t* completionEvent)
	{
		return VdsReadDisk(m_disk, clusterToSector(cluster), m_SectorsPerCluster, buffer, completionEvent);
	}

	uint64_t readClusterChain(uint64_t current)
	{
		uint64_t fatOffset = 0;
		size_t numSectors = 1;
		size_t valueShift = 0;
		size_t valueMask = 0xFFFF;
		switch (m_fatVer)
		{
		case FAT12:
			fatOffset = current + (current / 2);
			numSectors = 2;		//Might span a sector boundary
			break;
		case FAT16:
			fatOffset = current * 2;
			break;
		case FAT32:
			fatOffset = current * 4;
			valueMask = 0x0FFFFFFF;
			break;
		default:
			return UINT64_MAX;
		}
		uint64_t fatSector = m_FirstFatSector + (fatOffset / m_SectorSize);
		size_t entOffset = fatOffset % m_SectorSize;

		uint32_le* fatBuf = new uint32_le[m_SectorSize*numSectors];
		VdsReadDisk(m_disk, fatSector, numSectors, fatBuf, nullptr);
		uint32_t fatValue;
		if (m_fatVer <= FAT16)
			fatValue = LE_TO_CPU16(((uint16_le*)fatBuf)[entOffset]);
		else
			fatValue = LE_TO_CPU32(fatBuf[entOffset]);

		delete[] fatBuf;
		return fatValue;
	}

	uint32_t convertFatAttributes(uint8_t nativeFat)
	{
		uint32_t result = 0;
		if (nativeFat & HIDDEN)
			result |= VFS_ATTR_HIDDEN;
		if (nativeFat & READ_ONLY)
			result |= VFS_ATTR_READONLY;
		if (nativeFat & SYSTEM)
			result |= VFS_ATTR_SYSTEM;
		if (nativeFat & DIRECTORY)
			result |= VFS_ATTR_DIRECTORY;
		return result;
	}

	pfile searchDirectory(pfile directory, dir_search_callback callback, const void* callbackParam)
	{
		size_t directoryEntries = (m_SectorSize * m_SectorsPerCluster) / sizeof(FAT_DIRECTORY_ENTRY);
		file root_directory;
		PFAT_DIRECTORY_ENTRY buffer;
		uint64_t curCluster = 0;
		if (!directory)
		{
			uint64_t rootdirSector = m_FirstRootDirSector;
			//Root directory
			if (m_fatVer == FAT32)
			{
				//This is like any other directory, rootdirSector is a cluster.
				curCluster = root_directory.startCluster = rootdirSector;
				root_directory.isDirectory = true;
				//Now use standard directory handling code
				directory = &root_directory;
			}
			else
			{
				//Read entire root directory
				directoryEntries = (m_SectorSize * m_rootDirSectors) / sizeof(FAT_DIRECTORY_ENTRY);
				buffer = (PFAT_DIRECTORY_ENTRY)new uint8_t[m_SectorSize * m_rootDirSectors];
				auto status = VdsReadDisk(m_disk, rootdirSector, m_rootDirSectors, buffer, nullptr);
			}
		}
		if (directory)
		{
			buffer = (PFAT_DIRECTORY_ENTRY)new uint8_t[m_SectorSize * m_SectorsPerCluster];
			//Read first cluster
			ReadCluster(directory->startCluster, buffer, nullptr);
		}

		uint8_t* buf = (uint8_t*)buffer;
		size_t fileIndex = 0;
		//Generic code from here
		for (int i = 0; buffer[i].filename[0] != 0; ++i)
		{
			if (i == directoryEntries)
			{
				//TODO: Load next cluster
				auto newCluster = readClusterChain(curCluster);
				if (newCluster == UINT64_MAX)
					break;
				ReadCluster(newCluster, buffer, nullptr);
				i = 0;
				curCluster = newCluster;
			}
			char16_t* filename = nullptr;
			if (*(uint8_t*)&buffer[i] == 0xE5)
				continue;
			if (buffer[i].attributes & VOLUME_LABEL)
			{
				if (buffer[i].attributes != 0x0F)
				{
					filename = nullptr;
					continue;
				}
				//LFN
				PFAT_DIRECTORY_LFN lfn = (PFAT_DIRECTORY_LFN)&buffer[i];
				size_t sequenceNumber = (lfn->sequenceNumber & 0x3F);
				if ((lfn->sequenceNumber & 0x40) == 0)
				{
					kprintf(u"Error: unexpected LFN order\n");
					filename = nullptr;
					continue;
				}
				filename = new char16_t[sequenceNumber * 13 + 1];
				filename[sequenceNumber * 13] = 0;

				while (buffer[i].attributes == 0x0F)
				{

					memcpy(&filename[(sequenceNumber - 1) * 13 + 0], lfn->firstChars, sizeof(lfn->firstChars));
					memcpy(&filename[(sequenceNumber - 1) * 13 + 5], lfn->secondChars, sizeof(lfn->secondChars));
					memcpy(&filename[(sequenceNumber - 1) * 13 + 11], lfn->finalChars, sizeof(lfn->finalChars));
					++i;
					lfn = (PFAT_DIRECTORY_LFN)&buffer[i];
					sequenceNumber = (lfn->sequenceNumber & 0x3F);
				}
			}
			if (!filename)
			{
				filename = readClassicFilename(buffer[i]);
			}
			if (filename[0] == '.')
				continue;
			if (callback(filename, fileIndex, callbackParam))
			{
				//This is the file we want
				pfile fileEntry = new file;
				fileEntry->isDirectory = (buffer[i].attributes & DIRECTORY);
				fileEntry->filename = filename;
				fileEntry->startCluster = buffer[i].clusterLow | ((m_fatVer >= FAT32) ? (uint32_t)buffer[i].clusterHigh << 16 : 0);
				fileEntry->vfsAttributes = convertFatAttributes(buffer[i].attributes);
				return fileEntry;
			}
			else
				delete[] filename;
			++fileIndex;
		}
		return nullptr;
	}

	uint64_t clusterToSector(uint64_t cluster)
	{
		return (cluster - 2) * m_SectorsPerCluster + m_firstDataSector;
	}
private:
	const PFAT_BPB m_bpb;
	const HDISK m_disk;
	CHAIOS_FILESYSTEM_DRIVER m_driverinfo;
	enum FAT_VERSION {
		FAT12,
		FAT16,
		FAT32
	}m_fatVer;
	uint8_t readableFat(FAT_VERSION ver)
	{
		switch (ver)
		{
		case FAT12:
			return 12;
		case FAT16:
			return 16;
		case FAT32:
			return 32;
		default:
			return 0;
		};
	}

	enum FAT_ATTRIBUTES {
		READ_ONLY = 1,
		HIDDEN = 2,
		SYSTEM = 4,
		VOLUME_LABEL = 8,
		DIRECTORY = 16,
		ARCHIVE = 32,
		WEIRD0 = 64, // dunno the meaning for these (LFN ?)
		WEIRD1 = 128, //ditto here.
	};

	typedef struct _tag_fat_directory {
		char filename[11];		//8.3
		uint8_t attributes;
		uint8_t windowsNT;
		uint8_t creationTenths;
		uint16_le creationTime;
		uint16_le creationDate;
		uint16_le accessedDate;
		uint16_le clusterHigh;
		uint16_le modifiedTime;
		uint16_le modifiedDate;
		uint16_le clusterLow;
		uint32_le fileSize;
	}FAT_DIRECTORY_ENTRY, *PFAT_DIRECTORY_ENTRY;

	typedef struct _tag_fat_lfn {
		uint8_t sequenceNumber;
		char16_t firstChars[5];
		uint8_t attributes;		//0x0F
		uint8_t longEntryType;
		uint8_t sfnChecksum;
		char16_t secondChars[6];
		uint16_t reserved;
		char16_t finalChars[2];
	}FAT_DIRECTORY_LFN, *PFAT_DIRECTORY_LFN;

	char16_t* readClassicFilename(FAT_DIRECTORY_ENTRY& entry)
	{
		char16_t* filename = new char16_t[13];
		size_t spacePaddingOffset = 0;
		for (int ci = 0; ci < 8; ++ci)
		{
			filename[ci] = entry.filename[ci];
			if (filename[ci] != ' ')
				spacePaddingOffset = ci + 1;
		}
		filename[spacePaddingOffset] = '.';
		++spacePaddingOffset;
		for (int ci = 0; ci < 3; ++ci)
		{
			filename[spacePaddingOffset] = entry.filename[8 + ci];
			char16_t test[2];
			test[1] = 0;
			test[0] = filename[spacePaddingOffset];
			if (filename[spacePaddingOffset] == ' ')
			{
				//Remove the . if empty extension
				if (ci == 0)
					filename[spacePaddingOffset - 1] = '\0';
				break;
			}
			++spacePaddingOffset;
		}
		filename[spacePaddingOffset] = '\0';

		return filename;
	}

	size_t m_SectorSize;
	size_t m_SectorsPerCluster;
	size_t m_firstDataSector;
	size_t m_FirstFatSector;
	//Root cluster on FAT32
	size_t m_FirstRootDirSector;
	size_t m_rootDirSectors;

protected:
	pfile open(pfile directory, const char16_t* name, uint32_t mode, semaphore_t* asyncSem)
	{
		if(directory && !directory->isDirectory)
			return nullptr;
		pfile curFile = searchDirectory(directory, find_by_name, name);
		if (!curFile)
		{
			//TODO: check creation modes
			return nullptr;
		}
		//File exists
		return curFile;
	}
	void close(pfile file)
	{
		
	}
	ssize_t read(pfile file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem)
	{
		auto wstrlen = [](const char16_t* str)
		{
			size_t sz = 0;
			for (; str[sz]; ++sz);
			return sz;
		};
		//Read file or directory
		if (file && !file->isDirectory)
		{
			return 0;
		}
		else
		{
			PVFS_DIRECTORY_ENTRY entry = (PVFS_DIRECTORY_ENTRY)buffer;
			size_t idx = 0;
			for (; (idx + 1) * sizeof(VFS_DIRECTORY_ENTRY) <= requested; ++idx)
			{
				pfile result = searchDirectory(file, find_by_index, (const void*)(offset + idx));
				if (!result)
					break;
				size_t filenameLen = wstrlen(result->filename);
				memcpy(entry[idx].filename, result->filename, (filenameLen >= 255 ? 255 : filenameLen + 1) * 2);
				entry[idx].filename[255] = 0;
				entry[idx].attributes = result->vfsAttributes;
			}
			return -(idx);

		}
		return 0;
	}
	ssize_t write(pfile file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem)
	{
		return 0;
	}

	uint32_t attributes(pfile file)
	{
		if (!file)
			return 0;
		return file->vfsAttributes;
	}

	friend pfile fat_open(void* fsObject, pfile directory, const char16_t* name, uint32_t mode, semaphore_t* asyncSem);
	friend void fat_close(void* fsObject, pfile file);
	friend ssize_t fat_read(void* fsObject, pfile file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem);
	friend ssize_t fat_write(void* fsObject, pfile file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem);
	friend uint32_t fat_attributes(void* fsObject, pfile file);
};

pfile fat_open(void* fsObject, pfile directory, const char16_t* name, uint32_t mode, semaphore_t* asyncSem)
{
	return ((CFatVolume*)fsObject)->open(directory, name, mode, asyncSem);
}
void fat_close(void* fsObject, pfile file)
{
	((CFatVolume*)fsObject)->close(file);
}
ssize_t fat_read(void* fsObject, pfile file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem)
{
	return ((CFatVolume*)fsObject)->read(file, buffer, requested, offset, asyncSem);
}
ssize_t fat_write(void* fsObject, pfile file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem)
{
	return ((CFatVolume*)fsObject)->write(file, buffer, requested, offset, asyncSem);
}
uint32_t fat_attributes(void* fsObject, pfile file)
{
	return ((CFatVolume*)fsObject)->attributes(file);
}

static vds_enum_result FatVdsCallback(HDISK disk)
{
	auto params = VdsGetParams(disk);
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
	if (VdsReadDisk(disk, 0, 1, sector, nullptr) != 0)
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

	CFatVolume* volume = new CFatVolume(disk, bpb);
	volume->init();

	return RESULT_BOUND;
}


EXTERN int CppDriverEntry(void* param)
{
	//Register with VDS
	VdsRegisterFilesystem(FatVdsCallback);
	return 0;
}
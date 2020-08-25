#ifndef CHAIOS_VDS_H
#define CHAIOS_VDS_H

#include <chaikrnl.h>
#include <stdint.h>
#include <semaphore.h>
#include <guid.h>

typedef void* HDISK;
typedef uint64_t lba_t;
typedef uint64_t vds_length_t;

typedef uint32_t vds_err_t;

typedef vds_err_t(*chaios_vds_read)(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent);
typedef vds_err_t(*chaios_vds_write)(void* param, lba_t blockaddr, vds_length_t count, void* buffer, semaphore_t* completionEvent);
typedef vds_err_t(*chaios_vds_async_status)(void* param, vds_err_t token, semaphore_t completionEvent);

#pragma pack(push, 1)
typedef struct _CHAIOS_VDS_PARAMS {
	lba_t diskSize;
	uint64_t sectorSize;
	GUID diskType;
	GUID diskId;
	HDISK parent;
}CHAIOS_VDS_PARAMS, *PCHAIOS_VDS_PARAMS;

typedef PCHAIOS_VDS_PARAMS (*chaios_vds_params)(void* param);

typedef struct _CHAIOS_VDS_DISK {
	chaios_vds_read read_fn;
	chaios_vds_write write_fn;
	chaios_vds_params get_params;
	chaios_vds_async_status async_status;
	void* fn_param;
}CHAIOS_VDS_DISK, *PCHAIOS_VDS_DISK;

#pragma pack(pop)

#define CHAIOS_VDS_DISK_NVME MKGUID(0x641D1ED4, 0x26CE, 0x4875, 0xA2BF, 0x2B, 0x2D, 0xB3, 0x48, 0xD5, 0x4E)

#define CHAIOS_VDS_DISK_MBR(mbrtype) MKGUID(0x401E768B, 0x3AEB, 0x4A21, 0x88AC, 0xF8, 0xCD, 0x1E, 0x64, 0x61, mbrtype)

typedef uint8_t vds_enum_result;
typedef vds_enum_result (*chaios_vds_enum_callback)(HDISK disk);

enum VDS_ENUM_RESULT {
	RESULT_NOTBOUND,
	RESULT_BOUND,
	RESULT_NEWDISKS = UINT8_MAX
};

void init_vds();

EXTERN CHAIKRNL_FUNC HDISK RegisterVdsDisk(PCHAIOS_VDS_DISK diskInfo);
/*
	VDS READ
		* HDISK disk - Disk to read from
		* lba_t block - Block to read from
		* vds_length_t count -Number of blocks to read
		* void* buffer - Buffer to read to
		Synchronous operation: completionEvent = nullptr
			Returns status of completion
		Asynchronous operation: completionEvent -> semaphore_t
			Outputs to *completionEvent a semaphore representing the event
			Returns vds_err_t token, this must be passed to VdsGetStatusAsync after the operation completes to get status.
			Note that VdsGetStatusAsync *must* be called
*/
EXTERN CHAIKRNL_FUNC vds_err_t VdsReadDisk(HDISK disk, lba_t block, vds_length_t count, void* buffer, semaphore_t* completionEvent);
EXTERN CHAIKRNL_FUNC vds_err_t VdsWriteDisk(HDISK disk, lba_t block, vds_length_t count, void* buffer, semaphore_t* completionEvent);
EXTERN CHAIKRNL_FUNC vds_err_t VdsGetStatusAsync(HDISK disk, vds_err_t token, semaphore_t completionEvent);
EXTERN CHAIKRNL_FUNC PCHAIOS_VDS_PARAMS VdsGetParams(HDISK disk);

EXTERN CHAIKRNL_FUNC void enumerate_disks(chaios_vds_enum_callback callback);

EXTERN CHAIKRNL_FUNC void VdsRegisterFilesystem(chaios_vds_enum_callback callback);

void vds_start_filesystem_matching();

#endif

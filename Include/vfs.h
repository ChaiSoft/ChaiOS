#ifndef CHAIOS_VFS_H
#define CHAIOS_VFS_H

#include <chaikrnl.h>
#include <stdheaders.h>
#include <semaphore.h>

typedef struct _tag_file file, *pfile;

typedef uint64_t off_t;

typedef void* _chaios_VFS_HANDLE;
typedef _chaios_VFS_HANDLE HFILESYSTEM;
typedef _chaios_VFS_HANDLE HFILE;

enum VFS_OPEN_MODES {
	OPEN_MODE_READ = 1,
	OPEN_MODE_WRITE = 2,
	OPEN_MODE_CREATE = 4
};

enum VFS_FILE_ATTRIBUTES {
	VFS_ATTR_HIDDEN = 1,
	VFS_ATTR_SYSTEM = 2,
	VFS_ATTR_READONLY = 4,
	VFS_ATTR_DIRECTORY = 0x10
};

#pragma pack(push, 1)
typedef struct _chaios_filesystem_driver {
	pfile (*open)(void* fsObject, pfile directory, const char16_t* name, uint32_t mode, semaphore_t* asyncSem);
	void(*close)(void* fsObject, pfile file);
	ssize_t(*read)(void* fsObject, pfile file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem);
	ssize_t(*write)(void* fsObject, pfile file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem);
	uint32_t(*getAttributes)(void* fsObject, pfile file);
	void* fsObject;
}CHAIOS_FILESYSTEM_DRIVER, *PCHAIOS_FILESYSTEM_DRIVER;

typedef struct _tag_VFS_DIRECTORY_ENTRY {
	char16_t filename[256];
	HFILE parentDirectory;
	uint32_t attributes;
}VFS_DIRECTORY_ENTRY, *PVFS_DIRECTORY_ENTRY;
#pragma pack(pop)

EXTERN CHAIKRNL_FUNC HFILESYSTEM VfsRegisterFilesystem(PCHAIOS_FILESYSTEM_DRIVER fs);
EXTERN CHAIKRNL_FUNC HFILE VfsOpenFile(HFILE directory, const char16_t* name, uint32_t mode, semaphore_t* asyncSem);
EXTERN CHAIKRNL_FUNC ssize_t VfsReadFile(HFILE file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem);
EXTERN CHAIKRNL_FUNC uint32_t VfsGetAttributes(HFILE file);

EXTERN CHAIKRNL_FUNC char16_t VfsListRootVolumes(char16_t prev);

void vfsInit();

#endif

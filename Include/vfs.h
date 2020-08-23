#ifndef CHAIOS_VFS_H
#define CHAIOS_VFS_H

#include <chaikrnl.h>
#include <stdheaders.h>
#include <semaphore.h>

typedef struct _tag_file file, *pfile;

typedef uint64_t off_t;

#pragma pack(push, 1)
typedef struct _chaios_filesystem_driver {
	pfile (*open)(void* fsObject, pfile directory, const char16_t* name, uint32_t mode, semaphore_t* asyncSem);
	void(*close)(void* fsObject, pfile file);
	ssize_t(*read)(void* fsObject, pfile file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem);
	ssize_t(*write)(void* fsObject, pfile file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem);
	
	void* fsObject;
}CHAIOS_FILESYSTEM_DRIVER, *PCHAIOS_FILESYSTEM_DRIVER;
#pragma pack(pop)

CHAIKRNL_FUNC void VfsRegisterFilesystem(PCHAIOS_FILESYSTEM_DRIVER fs);

#endif

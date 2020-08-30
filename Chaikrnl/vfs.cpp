#include <vfs.h>
#include <redblack.h>
#include <ReadersWriterLock.h>

static size_t handle_alloc = 1;

typedef struct _tag_VFS_FILE {
	pfile internalFile;
}VFS_FILE, *PVFS_FILE;

struct internal_fs_info {
	PCHAIOS_FILESYSTEM_DRIVER filesystemDriver;
	//Only if it's a file
	VFS_FILE fileInfo;
};

static RedBlackTree<_chaios_VFS_HANDLE, internal_fs_info*> handle_translator;
static sharespinlock_t treelock;

static RedBlackTree<char16_t, HFILESYSTEM> rootVolumes;
size_t volumeAlloc = 'C';
static sharespinlock_t volumelock;

static bool inited = false;

CHAIKRNL_FUNC HFILESYSTEM VfsRegisterFilesystem(PCHAIOS_FILESYSTEM_DRIVER fs)
{
	size_t alloc = handle_alloc;
	while (!arch_cas(&handle_alloc, alloc, alloc + 1))
		alloc = handle_alloc;

	char16_t valloc = volumeAlloc;
	while (!arch_cas(&volumeAlloc, valloc, valloc + 1))
		valloc = volumeAlloc;

	HFILESYSTEM handle = (HFILESYSTEM)alloc;
	internal_fs_info* intinfo = new internal_fs_info;
	if (!intinfo)
		return NULL;
	intinfo->filesystemDriver = fs;
	auto st = SharedSpinlockAcquire(treelock, TRUE);
	handle_translator[handle] = intinfo;
	SharedSpinlockRelease(treelock, st);

	//This is the root directory
	intinfo->fileInfo.internalFile = nullptr;

	st = SharedSpinlockAcquire(volumelock, TRUE);
	rootVolumes[valloc] = handle;
	SharedSpinlockRelease(volumelock, st);
	return handle;
}
#include <kstdio.h>
CHAIKRNL_FUNC HFILE VfsOpenFile(HFILE directory, const char16_t* name, uint32_t mode, semaphore_t* asyncSem)
{
	internal_fs_info* intInfo = nullptr;
	if (!directory)
	{
		//Root volume
		char16_t vol = name[0];
		auto st = SharedSpinlockAcquire(volumelock, FALSE);
		auto it = rootVolumes.find(vol);
		if (it != rootVolumes.end())
			directory = it->second;
		SharedSpinlockRelease(volumelock, st);
		//Knock off volume identifier
		name += 3;
	}
	if (directory)
	{
		auto st = SharedSpinlockAcquire(treelock, FALSE);
		auto it = handle_translator.find(directory);
		if(it != handle_translator.end())
			intInfo = it->second;
		SharedSpinlockRelease(treelock, st);
	}
	
	if (!intInfo)
		return NULL;

	if (!name || name[0] == u'\0')
		return directory;

	auto fs = intInfo->filesystemDriver;
	pfile output = fs->open(fs->fsObject, intInfo->fileInfo.internalFile, name, mode, asyncSem);
	if (!output)
		return NULL;
	//Cache this
	internal_fs_info* fileInfo = new internal_fs_info;
	if (!fileInfo)
		return NULL;
	fileInfo->filesystemDriver = fs;
	fileInfo->fileInfo.internalFile = output;

	size_t alloc = handle_alloc;
	while (!arch_cas(&handle_alloc, alloc, alloc + 1))
		alloc = handle_alloc;
	HFILE fileHandle = (HFILE)alloc;
	auto st = SharedSpinlockAcquire(treelock, TRUE);
	handle_translator[fileHandle] = fileInfo;
	SharedSpinlockRelease(treelock, st);

	return fileHandle;
}

CHAIKRNL_FUNC ssize_t VfsReadFile(HFILE file, void* __user buffer, size_t requested, off_t offset, semaphore_t* asyncSem)
{
	auto st = SharedSpinlockAcquire(treelock, FALSE);
	internal_fs_info* intInfo = handle_translator[file];
	SharedSpinlockRelease(treelock, st);

	auto fs = intInfo->filesystemDriver;
	return fs->read(fs->fsObject, intInfo->fileInfo.internalFile, buffer, requested, offset, asyncSem);
}

CHAIKRNL_FUNC uint32_t VfsGetAttributes(HFILE file)
{
	auto st = SharedSpinlockAcquire(treelock, FALSE);
	internal_fs_info* intInfo = handle_translator[file];
	SharedSpinlockRelease(treelock, st);

	auto fs = intInfo->filesystemDriver;
	return fs->getAttributes(fs->fsObject, intInfo->fileInfo.internalFile);
}

EXTERN CHAIKRNL_FUNC char16_t VfsListRootVolumes(char16_t prev)
{
	char16_t retval = 0;
	auto st = SharedSpinlockAcquire(volumelock, FALSE);
	auto it = rootVolumes.find(prev);
	if (it == rootVolumes.end())
		it = rootVolumes.begin();
	else
		++it;
	if (it == rootVolumes.end())
		retval = 0;
	else
		retval = it->first;
	SharedSpinlockRelease(volumelock, st);
	return retval;
}

void vfsInit()
{
	if(inited)
		return;
	inited = true;
	treelock = SharedSpinlockCreate();
	volumelock = SharedSpinlockCreate();
}
#include "DataStoreInternal.h"
#include <stdio.h>
#include <errno.h>
#ifdef PLATFORM_POSIX
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <dirent.h>
#endif

NAMESPACE_UPP

Mutex s_canonical_write_mutex;



#ifdef PLATFORM_POSIX

bool DataStore::AcquireSourceLock(const String& root, bool shared, bool create_if_missing, int& lock_fd)
{
	lock_fd = -1;
	if(!IsFullPath(root.ToStd().c_str())) return false;
	int root_fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if(root_fd < 0) return false;
	Vector<String> parts = Split(root.Mid(1), '/');
	for(const String& part : parts) {
		if(part.IsEmpty()) continue;
		if(part == "." || part == "..") { close(root_fd); return false; }
		int next = openat(root_fd, part.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if(next < 0) { close(root_fd); return false; }
		close(root_fd);
		root_fd = next;
	}
	struct stat root_stat;
	if(fstat(root_fd, &root_stat) != 0 || !S_ISDIR(root_stat.st_mode) || root_stat.st_uid != geteuid() ||
	   (root_stat.st_mode & (S_IWGRP | S_IWOTH))) { close(root_fd); return false; }
	int open_flags = (create_if_missing ? O_RDWR | O_CREAT : O_RDONLY) | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
	int fd = openat(root_fd, ".mgr-write.lock", open_flags, 0600);
	int open_error = errno;
	close(root_fd);
	if(fd < 0 && !create_if_missing && open_error == ENOENT) return true;
	if(fd < 0) return false;
	struct stat st;
	if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 || (st.st_mode & 077)) {
		close(fd);
		return false;
	}
	if(create_if_missing && (fchmod(fd, 0600) != 0 || fstat(fd, &st) != 0 || (st.st_mode & 077))) { close(fd); return false; }
	while(flock(fd, shared ? LOCK_SH : LOCK_EX) != 0) {
		if(errno != EINTR) { close(fd); return false; }
	}
	lock_fd = fd;
	return true;
}

void DataStore::ReleaseSourceLock(int& lock_fd)
{
	if(lock_fd >= 0) { flock(lock_fd, LOCK_UN); close(lock_fd); lock_fd = -1; }
}

bool OpenRecordParent(int root_fd, const String& rel_path, int& parent_fd, String& leaf)
{
	Vector<String> parts = Split(rel_path, '/');
	if(parts.IsEmpty()) return false;
	for(const String& part : parts)
		if(part.IsEmpty() || part == "." || part == ".." || part.Find('\\') >= 0) return false;
	parent_fd = dup(root_fd);
	if(parent_fd < 0) return false;
	for(int i = 0; i + 1 < parts.GetCount(); i++) {
		int next = openat(parent_fd, parts[i].ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if(next < 0) { close(parent_fd); parent_fd = -1; return false; }
		struct stat dir_stat;
		if(fstat(next, &dir_stat) != 0 || !S_ISDIR(dir_stat.st_mode) || dir_stat.st_uid != geteuid() ||
		   (dir_stat.st_mode & (S_IWGRP | S_IWOTH))) {
			close(next); close(parent_fd); parent_fd = -1; return false;
		}
		close(parent_fd);
		parent_fd = next;
	}
	leaf = parts.Top();
	return true;
}

bool EnsureCreateRecordDirectory(int root_fd)
{
	if(mkdirat(root_fd, "records", 0700) != 0 && errno != EEXIST) return false;
	int fd = openat(root_fd, "records", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(fd < 0) return false;
	struct stat st;
	bool ok = fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == geteuid() &&
	          (st.st_mode & 077) == 0 && fsync(fd) == 0 && fsync(root_fd) == 0;
	if(close(fd) != 0) ok = false;
	return ok;
}

bool ReadRecordAt(int parent_fd, const String& leaf, String& bytes)
{
	int fd = openat(parent_fd, leaf.ToStd().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
	if(fd < 0) return false;
	struct stat st;
	if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH)) || st.st_size < 0 ||
	   st.st_size > DataStore::MAX_SOURCE_FILE_BYTES) { close(fd); return false; }
	bytes.Clear();
	char buffer[16384];
	for(;;) {
		ssize_t n = read(fd, buffer, sizeof(buffer));
		if(n < 0 && errno == EINTR) continue;
		if(n < 0) { close(fd); return false; }
		if(n == 0) break;
		bytes.Cat(buffer, (int)n);
		if(bytes.GetCount() > DataStore::MAX_SOURCE_FILE_BYTES) { close(fd); return false; }
	}
	return close(fd) == 0;
}
#endif

#ifndef PLATFORM_POSIX
bool DataStore::AcquireSourceLock(const String&, bool, bool create_if_missing, int& lock_fd) { lock_fd = -1; return !create_if_missing; }
void DataStore::ReleaseSourceLock(int& lock_fd) { lock_fd = -1; }
#endif

AtomicReplaceOutcome AtomicReplaceRecord(int parent_fd, const String& leaf, const String& bytes,
	                                                DataWriteTestFailpoint failpoint)
{
	if(failpoint == DATA_WRITE_FAIL_TEMP_CREATE || failpoint == DATA_WRITE_FAIL_TEMP_WRITE || failpoint == DATA_WRITE_FAIL_REPLACE)
		return ATOMIC_REPLACE_FAILED;
#ifdef PLATFORM_POSIX
	String temp = ".mgr-write-" + DataStore::ContentDigest(AsString(GetTickCount()) + AsString(Random64())).Mid(0, 24) + ".tmp";
	int fd = openat(parent_fd, temp.ToStd().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if(fd < 0) return ATOMIC_REPLACE_FAILED;
	bool ok = true;
	int64 written = 0;
	while(written < bytes.GetCount()) {
		ssize_t n = write(fd, bytes.Begin() + written, (size_t)(bytes.GetCount() - written));
		if(n < 0 && errno == EINTR) continue;
		if(n <= 0) { ok = false; break; }
		written += n;
	}
	if(ok && fsync(fd) != 0) ok = false;
	if(close(fd) != 0) ok = false;
	if(!ok) { unlinkat(parent_fd, temp.ToStd().c_str(), 0); return ATOMIC_REPLACE_FAILED; }
	if(renameat(parent_fd, temp.ToStd().c_str(), parent_fd, leaf.ToStd().c_str()) != 0) {
		unlinkat(parent_fd, temp.ToStd().c_str(), 0);
		return ATOMIC_REPLACE_FAILED;
	}
	bool durable = fsync(parent_fd) == 0;
	return durable ? ATOMIC_REPLACE_COMMITTED : ATOMIC_REPLACE_DURABILITY_UNCERTAIN;
#else
	(void)parent_fd; (void)leaf; (void)bytes;
	return ATOMIC_REPLACE_FAILED;
#endif
}

AtomicReplaceOutcome AtomicCreateRecord(int parent_fd, const String& leaf, const String& bytes)
{
#ifdef PLATFORM_POSIX
	String temp = ".mgr-create-" + DataStore::ContentDigest(AsString(GetTickCount()) + AsString(Random64())).Mid(0, 24) + ".tmp";
	int fd = openat(parent_fd, temp.ToStd().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if(fd < 0) return ATOMIC_REPLACE_FAILED;
	int64 written = 0;
	bool ok = true;
	while(written < bytes.GetCount()) {
		ssize_t n = write(fd, bytes.Begin() + written, (size_t)(bytes.GetCount() - written));
		if(n < 0 && errno == EINTR) continue;
		if(n <= 0) { ok = false; break; }
		written += n;
	}
	if(ok && fsync(fd) != 0) ok = false;
	if(close(fd) != 0) ok = false;
	if(!ok || linkat(parent_fd, temp.ToStd().c_str(), parent_fd, leaf.ToStd().c_str(), 0) != 0) {
		unlinkat(parent_fd, temp.ToStd().c_str(), 0);
		return ATOMIC_REPLACE_FAILED;
	}
	if(unlinkat(parent_fd, temp.ToStd().c_str(), 0) != 0 || fsync(parent_fd) != 0) return ATOMIC_REPLACE_DURABILITY_UNCERTAIN;
	return ATOMIC_REPLACE_COMMITTED;
#else
	(void)parent_fd; (void)leaf; (void)bytes;
	return ATOMIC_REPLACE_FAILED;
#endif
}

#ifdef PLATFORM_POSIX
#endif

END_UPP_NAMESPACE

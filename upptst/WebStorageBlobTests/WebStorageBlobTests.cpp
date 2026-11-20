#include <WebStorage/WebStorage.h>

using namespace Upp;

static int checks = 0;
static int failures = 0;

static bool Check(bool cond, const String& what)
{
	checks++;
	if(cond)
		Cout() << "ok    " << what << "\n";
	else {
		failures++;
		Cout() << "FAILED: " << what << "\n";
	}
	return cond;
}

static String Bytes(int n, int seed)
{
	String s;
	for(int i = 0; i < n; i++)
		s.Cat((char)((i * 7 + seed) & 0xff));
	return s;
}

CONSOLE_APP_MAIN
{
	String root = AppendFileName(GetCurrentDirectory(), "blob-selftest-data");
	DeleteFolderDeep(root);
	RealizeDirectory(root);

	const int64 limit = 1024;
	BlobStore store("selftest", root, "blobs", limit);
	String data = Bytes(64, 1);
	BlobRecord rec;

	StoreResult r = store.Put("img-1", data, "image/png", 8, 8, "", RecordAbsent(), rec);
	Check(r && rec.byte_size == 64 && rec.width == 8 && rec.height == 8 && rec.content_type == "image/png"
	      && rec.checksum == BlobStore::Checksum(data) && rec.checksum.GetCount() == 64,
	      "store: a blob is stored with its size, dimensions, content type and SHA-256 checksum");

	String back;
	BlobRecord meta;
	Check(store.Get("img-1", back, meta) && back == data && meta.checksum == rec.checksum
	      && meta.byte_size == 64 && meta.width == 8,
	      "read back: the bytes and the metadata come back unchanged");

	Check(store.Meta("img-1", meta) && meta.id == "img-1" && meta.content_type == "image/png",
	      "read back: metadata alone is readable");

	BlobRecord rec2;
	Check(store.Put("img-2", data, "image/png", 8, 8, rec.checksum, "", rec2) && rec2.checksum == rec.checksum,
	      "checksum: a correct declared checksum is accepted (case-insensitive)");

	String wrong = String('0', 64);
	BlobRecord rec3;
	StoreResult bad = store.Put("img-3", data, "image/png", 8, 8, wrong, "", rec3);
	Check(!bad && bad.error.Find("checksum mismatch") >= 0 && !store.Meta("img-3", meta),
	      "checksum mismatch: a wrong declared checksum is refused and nothing is stored");

	String path = AppendFileName(AppendFileName(AppendFileName(root, "data"), "blobs"), "img-1.blob");
	String tampered = Bytes(64, 2);
	back.Clear();
	Check(SaveFile(path, tampered) && !store.Get("img-1", back, meta) && back.IsEmpty(),
	      "checksum mismatch: stored bytes that no longer match the checksum are refused on read");

	back.Clear();
	Check(SaveFile(path, Bytes(63, 1)) && !store.Get("img-1", back, meta) && back.IsEmpty(),
	      "checksum mismatch: stored bytes of the wrong size are refused on read");

	String at_limit = Bytes((int)limit, 3);
	BlobRecord rec4;
	Check(store.Put("big-ok", at_limit, "application/octet-stream", 0, 0, "", "", rec4) && rec4.byte_size == limit,
	      "upload limit: a blob of exactly the limit is accepted");

	String over = Bytes((int)limit + 1, 4);
	BlobRecord rec5;
	StoreResult big = store.Put("big-no", over, "application/octet-stream", 0, 0, "", "", rec5);
	Check(!big && big.error.Find("upload limit") >= 0 && !store.Meta("big-no", meta),
	      "upload limit: a blob one byte over the limit is refused and nothing is stored");

	BlobRecord rec6;
	Check(!store.Put("empty", "", "image/png", 0, 0, "", "", rec6), "upload: an empty blob is refused");

	const char *bad_ids[] = { "", ".", "..", "a/b", "a\\b", "x y", "../escape", "img 1" };
	int refused = 0;
	for(const char *id : bad_ids) {
		BlobRecord rec7;
		StoreResult s = store.Put(id, data, "image/png", 8, 8, "", "", rec7);
		String b;
		BlobRecord m;
		if(!s && !store.Get(id, b, m))
			refused++;
	}
	Check(refused == 8, "id: every unsafe blob id is refused on store and on read");

	BlobRecord rec8;
	StoreResult conflict = store.Put("img-1", data, "image/png", 8, 8, "", RecordAbsent(), rec8);
	Check(!conflict && conflict.conflict, "revision: storing over an existing blob with RecordAbsent is a conflict");

	BlobRecord rec9;
	Check(!store.Put("mime", data, "image", 8, 8, "", "", rec9)
	      && !store.Put("dims", data, "image/png", -1, 8, "", "", rec9),
	      "validation: a malformed content type and negative dimensions are refused");

	String missing;
	BlobRecord mm;
	Check(!store.Get("never-stored", missing, mm), "read: a missing blob is refused");

	// Failed byte writes. A directory at the temp path makes the temp write fail; the store must then leave
	// the previous blob and its metadata exactly as they were.
	String blobs_dir = AppendFileName(AppendFileName(AppendFileName(root, "data"), "blobs"), "");
	String keep_old = Bytes(64, 5), keep_new = Bytes(64, 6);
	BlobRecord keep_rec;
	Check(store.Put("img-keep", keep_old, "image/png", 8, 8, "", RecordAbsent(), keep_rec).ok,
	      "failed write setup: a blob is stored before the write is obstructed");
	String temp_blocked = AppendFileName(blobs_dir, "img-keep.blob.tmp");
	RealizeDirectory(temp_blocked);
	BlobRecord keep_rec2;
	StoreResult failed = store.Put("img-keep", keep_new, "image/png", 8, 8, "", "", keep_rec2);
	String kept;
	BlobRecord kept_meta;
	Check(!failed && failed.error.Find("cannot write bytes") >= 0 && store.Get("img-keep", kept, kept_meta)
	      && kept == keep_old && kept_meta.checksum == BlobStore::Checksum(keep_old),
	      "failed byte write: the previous blob stays readable with its own metadata");
	DeleteFolderDeep(temp_blocked);

	// A failed rename after the metadata commit restores the metadata. The target is a directory, so the
	// rename of the new bytes onto it fails; a blob that did not exist before must not be left behind.
	String fresh_blocked = AppendFileName(blobs_dir, "img-fresh.blob");
	RealizeDirectory(fresh_blocked);
	BlobRecord fresh_rec;
	StoreResult fresh = store.Put("img-fresh", keep_new, "image/png", 8, 8, "", RecordAbsent(), fresh_rec);
	Check(!fresh && fresh.error.Find("cannot write bytes") >= 0 && !store.Meta("img-fresh", meta),
	      "failed rename of a new blob: its metadata is rolled back and nothing is left behind");
	DeleteFolderDeep(fresh_blocked);

	BlobRecord keep_rec3;
	Check(store.Put("img-keep", keep_new, "image/png", 8, 8, "", "", keep_rec3)
	      && store.Get("img-keep", kept, kept_meta) && kept == keep_new,
	      "failed write retry: once the obstruction is gone the overwrite stores and reads back");

	DeleteFolderDeep(root);
	Cout() << "\nchecks: " << checks << ", failures: " << failures << "\n";
	SetExitCode(failures ? 1 : 0);
}

#include "storage_manage.h"

#include <assert.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include "common/logging.h"
#include "leveldb/db.h"
#include "utils.h"

DECLARE_bool(ins_data_compress);
DECLARE_int32(ins_data_block_size);
DECLARE_int32(ins_data_write_buffer_size);

namespace galaxy {
namespace ins {

const std::string StorageManager::anonymous_user = "";

StorageManager::StorageManager(const std::string& data_dir)
    : data_dir_(data_dir) {
  bool ok = ins_common::Mkdirs(data_dir.c_str());
  if (!ok) {
    GLOG(FATAL) << "failed to create dir: " << data_dir;
  }
  // Create default database for shared namespace, i.e. anonymous user
  std::string full_name = data_dir + "/@db";
  leveldb::Options options;
  options.create_if_missing = true;
  if (FLAGS_ins_data_compress) {
    options.compression = leveldb::kSnappyCompression;
    GLOG(INFO) << "enable snappy compress for data storage " << full_name;
  }
  options.write_buffer_size = FLAGS_ins_data_write_buffer_size * 1024 * 1024;
  options.block_size = FLAGS_ins_data_block_size * 1024;
  GLOG(INFO) << "[data]: block_size: " << options.block_size
             << ", writer_buffer_size: " << options.write_buffer_size;
  leveldb::DB* default_db = NULL;
  leveldb::Status status = leveldb::DB::Open(options, full_name, &default_db);
  assert(status.ok());
  dbs_[anonymous_user] = default_db;
}

StorageManager::~StorageManager() {
  MutexLock lock(&mu_);
  for (auto it = dbs_.begin(); it != dbs_.end(); ++it) {
    delete it->second;
  }
  dbs_.clear();
}

bool StorageManager::OpenDatabase(const std::string& name) {
  {
    MutexLock lock(&mu_);
    if (dbs_.find(name) != dbs_.end()) {
      return true;
    }
  }
  std::string full_name = data_dir_ + "/" + name + "@db";
  leveldb::Options options;
  options.create_if_missing = true;
  if (FLAGS_ins_data_compress) {
    options.compression = leveldb::kSnappyCompression;
    GLOG(INFO) << "enable snappy compress for data storage " << full_name;
  }
  options.write_buffer_size = FLAGS_ins_data_write_buffer_size * 1024 * 1024;
  options.block_size = FLAGS_ins_data_block_size * 1024;
  GLOG(INFO) << "[data]: block_size: " << options.block_size
             << ", writer_buffer_size: " << options.write_buffer_size;
  leveldb::DB* current_db = NULL;
  leveldb::Status status = leveldb::DB::Open(options, full_name, &current_db);
  {
    MutexLock lock(&mu_);
    dbs_[name] = current_db;
  }
  return status.ok();
}

void StorageManager::CloseDatabase(const std::string& name) {
  MutexLock lock(&mu_);
  auto dbs_it = dbs_.find(name);
  if (dbs_it != dbs_.end()) {
    delete dbs_it->second;
    dbs_it->second = NULL;
    dbs_.erase(dbs_it);
  }
}

Status StorageManager::FindDB(const std::string& name, leveldb::DB** ret) {
  leveldb::DB* db_ptr = NULL;
  {
    MutexLock lock(&mu_);
    auto dbs_it = dbs_.find(name);
    if (dbs_it == dbs_.end()) {
      LOG(WARNING, "Inexist or unlogged user :%s", name.c_str());
      return kUnknownUser;
    }
    db_ptr = dbs_it->second;
    if (db_ptr == NULL) {
      LOG(WARNING, "Try to access a closing database :%s", name.c_str());
      return kError;
    }
  }
  *ret = db_ptr;
  return kOk;
}

Status StorageManager::Get(const std::string& name, const std::string& key,
                           std::string* value) {
  if (value == NULL) {
    return kError;
  }
  leveldb::DB* db_ptr = NULL;
  Status s = FindDB(name, &db_ptr);
  if (s != kOk) {
    return s;
  }
  leveldb::Status status = db_ptr->Get(leveldb::ReadOptions(), key, value);
  return (status.ok()) ? kOk : ((status.IsNotFound()) ? kNotFound : kError);
}

Status StorageManager::Put(const std::string& name, const std::string& key,
                           const std::string& value) {
  leveldb::DB* db_ptr = NULL;
  Status s = FindDB(name, &db_ptr);
  if (s != kOk) {
    return s;
  }
  leveldb::Status status = db_ptr->Put(leveldb::WriteOptions(), key, value);
  return (status.ok()) ? kOk : kError;
}

Status StorageManager::Delete(const std::string& name, const std::string& key) {
  leveldb::DB* db_ptr = NULL;
  Status s = FindDB(name, &db_ptr);
  if (s != kOk) {
    return s;
  }
  leveldb::Status status = db_ptr->Delete(leveldb::WriteOptions(), key);
  // Note: leveldb returns kOk even if the key is inexist
  return (status.ok()) ? kOk : kError;
}

StorageManager::Iterator* StorageManager::NewIterator(const std::string& name) {
  leveldb::DB* db_ptr = NULL;
  if (FindDB(name, &db_ptr) != kOk) {
    return NULL;
  }
  return new StorageManager::Iterator(db_ptr, leveldb::ReadOptions());
}

std::string StorageManager::Iterator::key() const {
  return (it_ != NULL) ? it_->key().ToString() : "";
}

std::string StorageManager::Iterator::value() const {
  return (it_ != NULL) ? it_->value().ToString() : "";
}

StorageManager::Iterator* StorageManager::Iterator::Seek(std::string key) {
  if (it_ != NULL) {
    it_->Seek(key);
  }
  return this;
}

StorageManager::Iterator* StorageManager::Iterator::Next() {
  if (it_ != NULL) {
    it_->Next();
  }
  return this;
}

bool StorageManager::Iterator::Valid() const {
  return (it_ != NULL) ? it_->Valid() : false;
}

Status StorageManager::Iterator::status() const {
  if (it_ != NULL) {
    if (it_->status().ok()) {
      return kOk;
    } else if (it_->status().IsNotFound()) {
      return kNotFound;
    }
  }
  return kError;
}

}  // namespace ins
}  // namespace galaxy

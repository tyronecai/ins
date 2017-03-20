#include "binlog.h"

#include <assert.h>
#include "common/asm_atomic.h"
#include "common/logging.h"
#include "leveldb/write_batch.h"
#include "utils.h"

namespace galaxy {
namespace ins {

const std::string log_dbname = "#binlog";
// 一个cursor，用于记录binlog的总长度，所以最后一个index是length - 1
const std::string length_tag = "#BINLOG_LEN#";

int32_t LogEntry::Dump(std::string* buf) const {
  assert(buf);
  int32_t total_len = sizeof(uint8_t) + sizeof(int32_t) + user.size() +
                      sizeof(int32_t) + key.size() + sizeof(int32_t) +
                      value.size() + sizeof(int64_t);
  buf->resize(total_len);
  const int32_t user_size = user.size();
  const int32_t key_size = key.size();
  const int32_t value_size = value.size();

  char* p = const_cast<char*>(buf->data());
  // op
  p[0] = static_cast<uint8_t>(op);
  p += sizeof(uint8_t);
  // user length & data
  memcpy(p, static_cast<const void*>(&user_size), sizeof(int32_t));
  p += sizeof(int32_t);
  memcpy(p, static_cast<const void*>(user.data()), user.size());
  p += user.size();
  // key length & data
  memcpy(p, static_cast<const void*>(&key_size), sizeof(int32_t));
  p += sizeof(int32_t);
  memcpy(p, static_cast<const void*>(key.data()), key.size());
  p += key.size();
  /// value length & data
  memcpy(p, static_cast<const void*>(&value_size), sizeof(int32_t));
  p += sizeof(int32_t);
  memcpy(p, static_cast<const void*>(value.data()), value.size());
  p += value.size();
  // term
  memcpy(p, static_cast<const void*>(&term), sizeof(int64_t));
  return total_len;
}

void LogEntry::Load(const std::string& buf) {
  const char* p = buf.data();
  int32_t user_size = 0;
  int32_t key_size = 0;
  int32_t value_size = 0;

  // op
  uint8_t opcode = 0;
  memcpy(static_cast<void*>(&opcode), p, sizeof(uint8_t));
  p += sizeof(uint8_t);
  op = static_cast<LogOperation>(opcode);

  // user
  memcpy(static_cast<void*>(&user_size), p, sizeof(int32_t));
  user.resize(user_size);
  p += sizeof(int32_t);
  memcpy(static_cast<void*>(&user[0]), p, user_size);
  p += user_size;

  // key
  memcpy(static_cast<void*>(&key_size), p, sizeof(int32_t));
  key.resize(key_size);
  p += sizeof(int32_t);
  memcpy(static_cast<void*>(&key[0]), p, key_size);
  p += key_size;

  // value
  memcpy(static_cast<void*>(&value_size), p, sizeof(int32_t));
  value.resize(value_size);
  p += sizeof(int32_t);
  memcpy(static_cast<void*>(&value[0]), p, value_size);
  p += value_size;

  // term
  memcpy(static_cast<void*>(&term), p, sizeof(int64_t));
}

BinLogger::BinLogger(const std::string& data_dir, bool compress,
                     int32_t block_size, int32_t write_buffer_size)
    : db_(NULL), length_(0), last_log_term_(-1) {
  bool ok = ins_common::Mkdirs(data_dir.c_str());
  if (!ok) {
    LOG(FATAL, "failed to create dir :%s", data_dir.c_str());
    abort();
  }
  std::string full_name = data_dir + "/" + log_dbname;
  leveldb::Options options;
  options.create_if_missing = true;
  if (compress) {
    options.compression = leveldb::kSnappyCompression;
    LOG(INFO, "enable snappy compress for binlog for %s", full_name.c_str());
  }
  options.write_buffer_size = write_buffer_size;
  options.block_size = block_size;
  LOG(INFO, "[binlog]: %s, block_size: %d, writer_buffer_size: %d",
      full_name.c_str(), options.block_size, options.write_buffer_size);
  leveldb::Status status = leveldb::DB::Open(options, full_name, &db_);
  if (!status.ok()) {
    LOG(FATAL, "failed to open db %s err %s", full_name.c_str(),
        status.ToString().c_str());
    assert(status.ok());
  }

  LOG(INFO, "try to init length & last log term from db");
  std::string value;
  status = db_->Get(leveldb::ReadOptions(), length_tag, &value);
  if (status.ok() && !value.empty()) {
    length_ = StringToInt(value);
    if (length_ > 0) {
      LogEntry log_entry;
      bool slot_ok = ReadSlot(length_ - 1, &log_entry);
      assert(slot_ok);
      last_log_term_ = log_entry.term;
      LOG(INFO, "get length: %ld, last log term: %ld", length_, last_log_term_);
    }
  }
}

BinLogger::~BinLogger() { delete db_; }

int64_t BinLogger::GetLength() {
  MutexLock lock(&mu_);
  return length_;
}

int64_t BinLogger::GetLastLogIndex() {
  MutexLock lock(&mu_);
  return length_ - 1;
}

void BinLogger::GetLastLogIndexAndTerm(int64_t* last_log_index,
                                       int64_t* last_log_term) {
  MutexLock lock(&mu_);
  // index从0开始计算
  *last_log_index = length_ - 1;
  *last_log_term = last_log_term_;
}

std::string BinLogger::IntToString(int64_t num) {
  std::string key;
  key.resize(sizeof(int64_t));
  memcpy(&key[0], &num, sizeof(int64_t));
  return key;
}

int64_t BinLogger::StringToInt(const std::string& s) {
  assert(s.size() == sizeof(int64_t));
  int64_t num = 0;
  memcpy(&num, &s[0], sizeof(int64_t));
  return num;
}

bool BinLogger::RemoveSlot(int64_t slot_index) {
  std::string value;
  std::string key = IntToString(slot_index);
  leveldb::Status status = db_->Get(leveldb::ReadOptions(), key, &value);
  if (!status.ok()) {
    return false;
  }
  status = db_->Delete(leveldb::WriteOptions(), key);
  if (status.ok()) {
    return true;
  } else {
    return false;
  }
}

bool BinLogger::RemoveSlotBefore(int64_t slot_gc_index) {
  db_->SetNexusGCKey(slot_gc_index);
  return true;
}

bool BinLogger::ReadSlot(int64_t slot_index, LogEntry* log_entry) {
  std::string value;
  std::string key = IntToString(slot_index);
  leveldb::Status status = db_->Get(leveldb::ReadOptions(), key, &value);
  if (status.ok()) {
    log_entry->Load(value);
    return true;
  } else if (status.IsNotFound()) {
    return false;
  } else {
    LOG(FATAL, "Read slot fail, %s", status.ToString().c_str());
    abort();
  }
}

void BinLogger::AppendEntryList(const ::google::protobuf::RepeatedPtrField<
    ::galaxy::ins::Entry>& entries) {
  leveldb::WriteBatch batch;
  {
    MutexLock lock(&mu_);
    int64_t cur_index = length_;
    std::string next_index = IntToString(length_ + entries.size());
    for (int i = 0; i < entries.size(); i++) {
      LogEntry log_entry;
      std::string buf;
      log_entry.op = entries.Get(i).op();
      log_entry.user = entries.Get(i).user();
      log_entry.key = entries.Get(i).key();
      log_entry.value = entries.Get(i).value();
      log_entry.term = entries.Get(i).term();
      log_entry.Dump(&buf);
      last_log_term_ = log_entry.term;
      batch.Put(IntToString(cur_index + i), buf);
    }
    batch.Put(length_tag, next_index);
    leveldb::Status status = db_->Write(leveldb::WriteOptions(), &batch);
    assert(status.ok());
    length_ += entries.size();
  }
}

void BinLogger::AppendEntry(const LogEntry& log_entry) {
  std::string buf;
  log_entry.Dump(&buf);
  {
    MutexLock lock(&mu_);
    leveldb::WriteBatch batch;
    batch.Put(IntToString(length_), buf);
    batch.Put(length_tag, IntToString(length_ + 1));
    leveldb::Status status = db_->Write(leveldb::WriteOptions(), &batch);
    assert(status.ok());

    ++length_;
    last_log_term_ = log_entry.term;
  }
}

void BinLogger::Truncate(int64_t trunk_slot_index) {
  if (trunk_slot_index < -1) {
    trunk_slot_index = -1;
  }

  {
    MutexLock lock(&mu_);
    length_ = trunk_slot_index + 1;
    leveldb::Status status =
        db_->Put(leveldb::WriteOptions(), length_tag, IntToString(length_));
    assert(status.ok());
    if (length_ > 0) {
      LogEntry log_entry;
      bool slot_ok = ReadSlot(length_ - 1, &log_entry);
      assert(slot_ok);
      last_log_term_ = log_entry.term;
    }
  }
}

}  // namespace ins
}  // namespace galaxy

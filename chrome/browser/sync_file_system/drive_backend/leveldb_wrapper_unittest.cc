// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/drive_backend/leveldb_wrapper.h"

#include <string>

#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/leveldatabase/src/helpers/memenv/memenv.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"
#include "third_party/leveldatabase/src/include/leveldb/env.h"

namespace sync_file_system {
namespace drive_backend {

struct TestData {
  const std::string key;
  const std::string value;
};

class LevelDBWrapperTest : public testing::Test {
 public:
  virtual ~LevelDBWrapperTest() {}

  virtual void SetUp() OVERRIDE {
    ASSERT_TRUE(database_dir_.CreateUniqueTempDir());
    in_memory_env_.reset(leveldb::NewMemEnv(leveldb::Env::Default()));
    InitializeLevelDB();
  }

  virtual void TearDown() OVERRIDE {
    db_.reset();
    in_memory_env_.reset();
  }

  void CreateDefaultDatabase() {
    leveldb::DB* db = db_->GetLevelDB();

    // Expected contents are
    // {"a": "1", "ab": "0", "bb": "3", "d": "4"}
    const char* keys[] = {"ab", "a", "d", "bb", "d"};
    for (size_t i = 0; i < arraysize(keys); ++i) {
      leveldb::Status status =
          db->Put(leveldb::WriteOptions(), keys[i], base::Int64ToString(i));
      ASSERT_TRUE(status.ok());
    }
  }

  LevelDBWrapper* GetDB() {
    return db_.get();
  }

  void CheckDBContents(const TestData expects[], size_t size) {
    DCHECK(db_);

    scoped_ptr<LevelDBWrapper::Iterator> itr = db_->NewIterator();
    itr->SeekToFirst();
    for (size_t i = 0; i < size; ++i) {
      ASSERT_TRUE(itr->Valid());
      EXPECT_EQ(expects[i].key, itr->key().ToString());
      EXPECT_EQ(expects[i].value, itr->value().ToString());
      itr->Next();
    }
    EXPECT_FALSE(itr->Valid());
  }

 private:
  void InitializeLevelDB() {
    leveldb::DB* db = NULL;
    leveldb::Options options;
    options.create_if_missing = true;
    options.max_open_files = 0;  // Use minimum.
    options.env = in_memory_env_.get();
    leveldb::Status status =
        leveldb::DB::Open(options, database_dir_.path().AsUTF8Unsafe(), &db);
    ASSERT_TRUE(status.ok());

    db_.reset(new LevelDBWrapper(make_scoped_ptr(db)));
  }

  base::ScopedTempDir database_dir_;
  scoped_ptr<leveldb::Env> in_memory_env_;
  scoped_ptr<LevelDBWrapper> db_;
};

TEST_F(LevelDBWrapperTest, IteratorTest) {
  CreateDefaultDatabase();
  scoped_ptr<LevelDBWrapper::Iterator> itr = GetDB()->NewIterator();

  itr->Seek("a");
  EXPECT_TRUE(itr->Valid());
  EXPECT_EQ("a", itr->key().ToString());
  EXPECT_EQ("1", itr->value().ToString());

  itr->Next();
  EXPECT_TRUE(itr->Valid());
  EXPECT_EQ("ab", itr->key().ToString());
  EXPECT_EQ("0", itr->value().ToString());

  itr->Seek("b");
  EXPECT_TRUE(itr->Valid());
  EXPECT_EQ("bb", itr->key().ToString());
  EXPECT_EQ("3", itr->value().ToString());

  itr->Next();
  EXPECT_TRUE(itr->Valid());
  EXPECT_EQ("d", itr->key().ToString());
  EXPECT_EQ("4", itr->value().ToString());

  itr->Next();
  EXPECT_FALSE(itr->Valid());

  itr->SeekToFirst();
  EXPECT_TRUE(itr->Valid());
  EXPECT_EQ("a", itr->key().ToString());
  EXPECT_EQ("1", itr->value().ToString());

  itr->Delete();
  EXPECT_TRUE(itr->Valid());
  EXPECT_EQ("ab", itr->key().ToString());
  EXPECT_EQ("0", itr->value().ToString());

  itr->SeekToFirst();
  EXPECT_TRUE(itr->Valid());
  EXPECT_EQ("ab", itr->key().ToString());
  EXPECT_EQ("0", itr->value().ToString());

  EXPECT_EQ(0, GetDB()->num_puts());
  EXPECT_EQ(1, GetDB()->num_deletes());
}

TEST_F(LevelDBWrapperTest, Iterator2Test) {
  GetDB()->Put("a", "1");
  GetDB()->Put("b", "2");
  GetDB()->Put("c", "3");
  // Keep pending transanctions on memory.

  scoped_ptr<LevelDBWrapper::Iterator> itr = GetDB()->NewIterator();

  std::string prev_key;
  std::string prev_value;
  int loop_counter = 0;
  for (itr->SeekToFirst(); itr->Valid(); itr->Next()) {
    ASSERT_NE(prev_key, itr->key().ToString());
    ASSERT_NE(prev_value, itr->value().ToString());
    prev_key = itr->key().ToString();
    prev_value = itr->value().ToString();
    ++loop_counter;
  }
  EXPECT_EQ(3, loop_counter);
  EXPECT_EQ("c", prev_key);
  EXPECT_EQ("3", prev_value);

  EXPECT_EQ(3, GetDB()->num_puts());
  EXPECT_EQ(0, GetDB()->num_deletes());
}

TEST_F(LevelDBWrapperTest, PutTest) {
  TestData merged_data[] = {{"a", "1"}, {"aa", "new0"}, {"ab", "0"},
                            {"bb", "new2"}, {"c", "new1"}, {"d", "4"}};
  TestData orig_data[] = {{"a", "1"}, {"ab", "0"}, {"bb", "3"}, {"d", "4"}};

  CreateDefaultDatabase();

  // Add pending transactions.
  GetDB()->Put("aa", "new0");
  GetDB()->Put("c", "new1");
  GetDB()->Put("bb", "new2");  // Overwrite an entry.

  SCOPED_TRACE("PutTest_Pending");
  CheckDBContents(merged_data, arraysize(merged_data));

  EXPECT_EQ(3, GetDB()->num_puts());
  // Remove all pending transactions.
  GetDB()->Clear();
  EXPECT_EQ(0, GetDB()->num_puts());

  SCOPED_TRACE("PutTest_Clear");
  CheckDBContents(orig_data, arraysize(orig_data));

  // Add pending transactions again, with commiting.
  GetDB()->Put("aa", "new0");
  GetDB()->Put("c", "new1");
  GetDB()->Put("bb", "new2");
  EXPECT_EQ(3, GetDB()->num_puts());
  GetDB()->Commit();
  EXPECT_EQ(0, GetDB()->num_puts());
  GetDB()->Clear();  // Clear just in case.

  SCOPED_TRACE("PutTest_Commit");
  CheckDBContents(merged_data, arraysize(merged_data));
}

TEST_F(LevelDBWrapperTest, DeleteTest) {
  TestData merged_data[] = {{"a", "1"}, {"aa", "new0"}, {"bb", "new2"},
                            {"d", "4"}};
  TestData orig_data[] = {{"a", "1"}, {"ab", "0"}, {"bb", "3"}, {"d", "4"}};

  CreateDefaultDatabase();

  // Add pending transactions.
  GetDB()->Put("aa", "new0");
  GetDB()->Put("c", "new1");
  GetDB()->Put("bb", "new2");  // Overwrite an entry.
  GetDB()->Delete("c");        // Remove a pending entry.
  GetDB()->Delete("ab");       // Remove a committed entry.

  EXPECT_EQ(3, GetDB()->num_puts());
  EXPECT_EQ(2, GetDB()->num_deletes());

  SCOPED_TRACE("DeleteTest_Pending");
  CheckDBContents(merged_data, arraysize(merged_data));

  // Remove all pending transactions.
  GetDB()->Clear();

  SCOPED_TRACE("DeleteTest_Clear");
  CheckDBContents(orig_data, arraysize(orig_data));

  // Add pending transactions again, with commiting.
  GetDB()->Put("aa", "new0");
  GetDB()->Put("c", "new1");
  GetDB()->Put("bb", "new2");
  GetDB()->Delete("c");
  GetDB()->Delete("ab");
  GetDB()->Commit();
  GetDB()->Clear();

  EXPECT_EQ(0, GetDB()->num_puts());
  EXPECT_EQ(0, GetDB()->num_deletes());

  SCOPED_TRACE("DeleteTest_Commit");
  CheckDBContents(merged_data, arraysize(merged_data));
}

}  // namespace drive_backend
}  // namespace sync_file_system

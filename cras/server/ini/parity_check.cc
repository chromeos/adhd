// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <format>
#include <iostream>
#include <memory>

#include "cras/common/rust_common.h"
#include "cras/server/ini/ini.h"
#include "gtest/gtest.h"
#include "iniparser.h"

class MyFixture : public testing::Test {};

class MyTest : public MyFixture {
 public:
  explicit MyTest(std::string path) : path_(path) {}
  void TestBody() override {
    ASSERT_EQ(
        testing::UnitTest::GetInstance()->current_test_info()->value_param(),
        path_);

    std::clog << "testing " << path_ << std::endl;

    std::unique_ptr<CrasIniDict, decltype(cras_ini_free)*> cras_ini_dict{
        cras_ini_load(path_.c_str()), cras_ini_free};
    ASSERT_TRUE(cras_ini_dict);

    std::unique_ptr<dictionary, decltype(iniparser_freedict)*> iniparser_dict{
        iniparser_load(path_.c_str()), iniparser_freedict};
    ASSERT_TRUE(iniparser_dict);

    int nsections = cras_ini_getnsec(cras_ini_dict.get());
    ASSERT_EQ(nsections, iniparser_getnsec(iniparser_dict.get()));

    for (int i = 0; i < nsections; i++) {
      std::string section = cras_ini_getsecname(cras_ini_dict.get(), i);
      ASSERT_EQ(section, iniparser_getsecname(iniparser_dict.get(), i))
          << std::format("i = {}", i);

      int nkeys = cras_ini_getsecnkeys(cras_ini_dict.get(), section.c_str());
      ASSERT_EQ(nkeys,
                iniparser_getsecnkeys(iniparser_dict.get(), section.c_str()))
          << std::format("section = {}", section);

      for (int j = 0; j < nkeys; ++j) {
        std::string key =
            cras_ini_getseckey(cras_ini_dict.get(), section.c_str(), j);
        std::string section_and_key = std::format("{}:{}", section, key);

        EXPECT_STREQ(
            cras_ini_getstring(cras_ini_dict.get(), section_and_key.c_str(),
                               // Note: we use a different notfound value
                               // because the key must exist.
                               "<cras_ini not found>"),
            iniparser_getstring(iniparser_dict.get(), section_and_key.c_str(),
                                "<iniparser not found>"))
            << std::format("section_and_key = {}", section_and_key);

        EXPECT_EQ(cras_ini_getint(cras_ini_dict.get(), section_and_key.c_str(),
                                  // Note: we use a different notfound value
                                  // because the key must exist.
                                  -2147483647),
                  iniparser_getint(iniparser_dict.get(),
                                   section_and_key.c_str(), -2147483648))
            << std::format("section_and_key = {}", section_and_key);
      }
    }
  }

 private:
  std::string path_;
};

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  cras_rust_init_logging();

  // See also for registering tests:
  // https://google.github.io/googletest/advanced.html#registering-tests-programmatically
  for (int i = 1; i < argc; i++) {
    testing::RegisterTest(
        "Ini", "ParityCheck", nullptr, argv[i], __FILE__, __LINE__,
        // Important to use the fixture type as the return type here.
        [=]() -> MyFixture* { return new MyTest(argv[i]); });
  }
  return RUN_ALL_TESTS();
}

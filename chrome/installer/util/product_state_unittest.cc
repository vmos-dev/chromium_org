// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>

#include "base/strings/utf_string_conversions.h"
#include "base/test/test_reg_util_win.h"
#include "base/version.h"
#include "base/win/registry.h"
#include "chrome/installer/util/browser_distribution.h"
#include "chrome/installer/util/google_update_constants.h"
#include "chrome/installer/util/installation_state.h"
#include "chrome/installer/util/product_unittest.h"
#include "chrome/installer/util/util_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::win::RegKey;
using installer::ProductState;
using registry_util::RegistryOverrideManager;

class ProductStateTest : public testing::Test {
 protected:
  static void SetUpTestCase();
  static void TearDownTestCase();

  virtual void SetUp();
  virtual void TearDown();

  void ApplyUninstallCommand(const wchar_t* exe_path, const wchar_t* args);
  void MinimallyInstallProduct(const wchar_t* version);

  static BrowserDistribution* dist_;
  bool system_install_;
  HKEY overridden_;
  registry_util::RegistryOverrideManager registry_override_manager_;
  RegKey clients_;
  RegKey client_state_;
};

BrowserDistribution* ProductStateTest::dist_;

// static
void ProductStateTest::SetUpTestCase() {
  testing::Test::SetUpTestCase();

  // We'll use Chrome as our test subject.
  dist_ = BrowserDistribution::GetSpecificDistribution(
      BrowserDistribution::CHROME_BROWSER);
}

// static
void ProductStateTest::TearDownTestCase() {
  dist_ = NULL;

  testing::Test::TearDownTestCase();
}

void ProductStateTest::SetUp() {
  testing::Test::SetUp();

  // Create/open the keys for the product we'll test.
  system_install_ = true;
  overridden_ = (system_install_ ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER);

  // Override for test purposes.  We don't use ScopedRegistryKeyOverride
  // directly because it doesn't suit itself to our use here.
  RegKey temp_key;

  registry_override_manager_.OverrideRegistry(overridden_);

  EXPECT_EQ(ERROR_SUCCESS,
            clients_.Create(overridden_, dist_->GetVersionKey().c_str(),
                            KEY_ALL_ACCESS));
  EXPECT_EQ(ERROR_SUCCESS,
            client_state_.Create(overridden_, dist_->GetStateKey().c_str(),
                                 KEY_ALL_ACCESS));
}

void ProductStateTest::TearDown() {
  // Done with the keys.
  client_state_.Close();
  clients_.Close();
  overridden_ = NULL;
  system_install_ = false;

  testing::Test::TearDown();
}

void ProductStateTest::MinimallyInstallProduct(const wchar_t* version) {
  EXPECT_EQ(ERROR_SUCCESS,
            clients_.WriteValue(google_update::kRegVersionField, version));
}

void ProductStateTest::ApplyUninstallCommand(const wchar_t* exe_path,
                                             const wchar_t* args) {
  if (exe_path == NULL) {
    LONG result = client_state_.DeleteValue(installer::kUninstallStringField);
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
  } else {
    EXPECT_EQ(ERROR_SUCCESS,
              client_state_.WriteValue(installer::kUninstallStringField,
                                       exe_path));
  }

  if (args == NULL) {
    LONG result =
        client_state_.DeleteValue(installer::kUninstallArgumentsField);
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
  } else {
    EXPECT_EQ(ERROR_SUCCESS,
              client_state_.WriteValue(installer::kUninstallArgumentsField,
                                       args));
  }
}

TEST_F(ProductStateTest, InitializeInstalled) {
  // Not installed.
  {
    ProductState state;
    LONG result = clients_.DeleteValue(google_update::kRegVersionField);
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_FALSE(state.Initialize(system_install_, dist_));
  }

  // Empty version.
  {
    ProductState state;
    LONG result = clients_.WriteValue(google_update::kRegVersionField, L"");
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_FALSE(state.Initialize(system_install_, dist_));
  }

  // Bogus version.
  {
    ProductState state;
    LONG result = clients_.WriteValue(google_update::kRegVersionField,
                                      L"goofy");
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_FALSE(state.Initialize(system_install_, dist_));
  }

  // Valid "pv" value.
  {
    ProductState state;
    LONG result = clients_.WriteValue(google_update::kRegVersionField,
                                      L"10.0.47.0");
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_EQ("10.0.47.0", state.version().GetString());
  }
}

// Test extraction of the "opv" value from the Clients key.
TEST_F(ProductStateTest, InitializeOldVersion) {
  MinimallyInstallProduct(L"10.0.1.1");

  // No "opv" value.
  {
    ProductState state;
    LONG result = clients_.DeleteValue(google_update::kRegOldVersionField);
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.old_version() == NULL);
  }

  // Empty "opv" value.
  {
    ProductState state;
    LONG result = clients_.WriteValue(google_update::kRegOldVersionField, L"");
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.old_version() == NULL);
  }

  // Bogus "opv" value.
  {
    ProductState state;
    LONG result = clients_.WriteValue(google_update::kRegOldVersionField,
                                      L"coming home");
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.old_version() == NULL);
  }

  // Valid "opv" value.
  {
    ProductState state;
    LONG result = clients_.WriteValue(google_update::kRegOldVersionField,
                                      L"10.0.47.0");
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.old_version() != NULL);
    EXPECT_EQ("10.0.47.0", state.old_version()->GetString());
  }
}

// Test extraction of the "cmd" value from the Clients key.
TEST_F(ProductStateTest, InitializeRenameCmd) {
  MinimallyInstallProduct(L"10.0.1.1");

  // No "cmd" value.
  {
    ProductState state;
    LONG result = clients_.DeleteValue(google_update::kRegRenameCmdField);
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.rename_cmd().empty());
  }

  // Empty "cmd" value.
  {
    ProductState state;
    LONG result = clients_.WriteValue(google_update::kRegRenameCmdField, L"");
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.rename_cmd().empty());
  }

  // Valid "cmd" value.
  {
    ProductState state;
    LONG result = clients_.WriteValue(google_update::kRegRenameCmdField,
                                      L"spam.exe --spamalot");
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_EQ(L"spam.exe --spamalot", state.rename_cmd());
  }
}

// Test extraction of the "ap" value from the ClientState key.
TEST_F(ProductStateTest, InitializeChannelInfo) {
  MinimallyInstallProduct(L"10.0.1.1");

  // No "ap" value.
  {
    ProductState state;
    LONG result = client_state_.DeleteValue(google_update::kRegApField);
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.channel().value().empty());
  }

  // Empty "ap" value.
  {
    ProductState state;
    LONG result = client_state_.WriteValue(google_update::kRegApField, L"");
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.channel().value().empty());
  }

  // Valid "ap" value.
  {
    ProductState state;
    LONG result = client_state_.WriteValue(google_update::kRegApField, L"spam");
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_EQ(L"spam", state.channel().value());
  }
}

// Test extraction of the uninstall command and arguments from the ClientState
// key.
TEST_F(ProductStateTest, InitializeUninstallCommand) {
  MinimallyInstallProduct(L"10.0.1.1");

  // No uninstall command.
  {
    ProductState state;
    ApplyUninstallCommand(NULL, NULL);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.GetSetupPath().empty());
    EXPECT_TRUE(state.uninstall_command().GetCommandLineString().empty());
    EXPECT_TRUE(state.uninstall_command().GetSwitches().empty());
  }

  // Empty values.
  {
    ProductState state;
    ApplyUninstallCommand(L"", L"");
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.GetSetupPath().empty());
    EXPECT_TRUE(state.uninstall_command().GetCommandLineString().empty());
    EXPECT_TRUE(state.uninstall_command().GetSwitches().empty());
  }

  // Uninstall command without exe.
  {
    ProductState state;
    ApplyUninstallCommand(NULL, L"--uninstall");
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.GetSetupPath().empty());
    EXPECT_EQ(L" --uninstall",
              state.uninstall_command().GetCommandLineString());
    EXPECT_EQ(1U, state.uninstall_command().GetSwitches().size());
  }

  // Uninstall command without args.
  {
    ProductState state;
    ApplyUninstallCommand(L"setup.exe", NULL);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_EQ(L"setup.exe", state.GetSetupPath().value());
    EXPECT_EQ(L"setup.exe", state.uninstall_command().GetCommandLineString());
    EXPECT_TRUE(state.uninstall_command().GetSwitches().empty());
  }

  // Uninstall command with exe that requires quoting.
  {
    ProductState state;
    ApplyUninstallCommand(L"set up.exe", NULL);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_EQ(L"set up.exe", state.GetSetupPath().value());
    EXPECT_EQ(L"\"set up.exe\"",
              state.uninstall_command().GetCommandLineString());
    EXPECT_TRUE(state.uninstall_command().GetSwitches().empty());
  }

  // Uninstall command with both exe and args.
  {
    ProductState state;
    ApplyUninstallCommand(L"setup.exe", L"--uninstall");
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_EQ(L"setup.exe", state.GetSetupPath().value());
    EXPECT_EQ(L"setup.exe --uninstall",
              state.uninstall_command().GetCommandLineString());
    EXPECT_EQ(1U, state.uninstall_command().GetSwitches().size());
  }
}

// Test extraction of the msi marker from the ClientState key.
TEST_F(ProductStateTest, InitializeMsi) {
  MinimallyInstallProduct(L"10.0.1.1");

  // No msi marker.
  {
    ProductState state;
    LONG result = client_state_.DeleteValue(google_update::kRegMSIField);
    EXPECT_TRUE(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_FALSE(state.is_msi());
  }

  // Msi marker set to zero.
  {
    ProductState state;
    EXPECT_EQ(ERROR_SUCCESS,
              client_state_.WriteValue(google_update::kRegMSIField,
                                       static_cast<DWORD>(0)));
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_FALSE(state.is_msi());
  }

  // Msi marker set to one.
  {
    ProductState state;
    EXPECT_EQ(ERROR_SUCCESS,
              client_state_.WriteValue(google_update::kRegMSIField,
                                       static_cast<DWORD>(1)));
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.is_msi());
  }

  // Msi marker set to a bogus DWORD.
  {
    ProductState state;
    EXPECT_EQ(ERROR_SUCCESS,
              client_state_.WriteValue(google_update::kRegMSIField,
                                       static_cast<DWORD>(47)));
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.is_msi());
  }

  // Msi marker set to a bogus string.
  {
    ProductState state;
    EXPECT_EQ(ERROR_SUCCESS,
              client_state_.WriteValue(google_update::kRegMSIField,
                                       L"bogus!"));
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_FALSE(state.is_msi());
  }
}

// Test detection of multi-install.
TEST_F(ProductStateTest, InitializeMultiInstall) {
  MinimallyInstallProduct(L"10.0.1.1");

  // No uninstall command means single install.
  {
    ProductState state;
    ApplyUninstallCommand(NULL, NULL);
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_FALSE(state.is_multi_install());
  }

  // Uninstall command without --multi-install is single install.
  {
    ProductState state;
    ApplyUninstallCommand(L"setup.exe", L"--uninstall");
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_FALSE(state.is_multi_install());
  }

  // Uninstall command with --multi-install is multi install.
  {
    ProductState state;
    ApplyUninstallCommand(L"setup.exe",
                          L"--uninstall --chrome --multi-install");
    EXPECT_TRUE(state.Initialize(system_install_, dist_));
    EXPECT_TRUE(state.is_multi_install());
  }
}

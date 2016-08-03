// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/user_prefs/tracked/registry_hash_store_contents_win.h"

#include <windows.h>

#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "base/win/registry.h"

using base::win::RegistryValueIterator;

namespace {

constexpr size_t kMacSize = 32;

base::string16 GetSplitPrefKeyName(const base::string16& reg_key_name,
                                   const std::string& split_key_name) {
  return reg_key_name + L"\\" + base::UTF8ToUTF16(split_key_name);
}

bool ReadMacFromRegistry(const base::win::RegKey& key,
                         const std::string& value_name,
                         std::string* out_mac) {
  base::string16 string_value;
  if (key.ReadValue(base::UTF8ToUTF16(value_name).c_str(), &string_value) ==
          ERROR_SUCCESS &&
      string_value.size() == kMacSize) {
    out_mac->assign(base::UTF16ToUTF8(string_value));
    return true;
  }
  return false;
}

// Removes |value_name| under |reg_key_name|. Returns true if found and
// successfully removed.
bool ClearAtomicMac(const base::string16& reg_key_name,
                    const std::string& value_name) {
  base::win::RegKey key;
  if (key.Open(HKEY_CURRENT_USER, reg_key_name.c_str(),
               KEY_SET_VALUE | KEY_WOW64_32KEY) == ERROR_SUCCESS) {
    return key.DeleteValue(base::UTF8ToUTF16(value_name).c_str()) ==
           ERROR_SUCCESS;
  }
  return false;
}

// Deletes |split_key_name| under |reg_key_name|. Returns true if found and
// successfully removed.
bool ClearSplitMac(const base::string16& reg_key_name,
                   const std::string& split_key_name) {
  base::win::RegKey key;
  if (key.Open(HKEY_CURRENT_USER,
               GetSplitPrefKeyName(reg_key_name, split_key_name).c_str(),
               KEY_SET_VALUE | KEY_WOW64_32KEY) == ERROR_SUCCESS) {
    return key.DeleteKey(L"") == ERROR_SUCCESS;
  }
  return false;
}

}  // namespace

RegistryHashStoreContentsWin::RegistryHashStoreContentsWin(
    const base::string16& registry_path,
    const base::string16& profile_name)
    : preference_key_name_(registry_path + L"\\PreferenceMACs\\" +
                           profile_name) {}

void RegistryHashStoreContentsWin::Reset() {
  base::win::RegKey key;
  if (key.Open(HKEY_CURRENT_USER, preference_key_name_.c_str(),
               KEY_SET_VALUE | KEY_WOW64_32KEY) == ERROR_SUCCESS) {
    LONG result = key.DeleteKey(L"");
    DCHECK(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND) << result;
  }
}

bool RegistryHashStoreContentsWin::GetMac(const std::string& path,
                                          std::string* out_value) {
  base::win::RegKey key;
  if (key.Open(HKEY_CURRENT_USER, preference_key_name_.c_str(),
               KEY_QUERY_VALUE | KEY_WOW64_32KEY) == ERROR_SUCCESS) {
    return ReadMacFromRegistry(key, path, out_value);
  }

  return false;
}

bool RegistryHashStoreContentsWin::GetSplitMacs(
    const std::string& path,
    std::map<std::string, std::string>* split_macs) {
  DCHECK(split_macs);
  DCHECK(split_macs->empty());

  RegistryValueIterator iter_key(
      HKEY_CURRENT_USER,
      GetSplitPrefKeyName(preference_key_name_, path).c_str());

  for (; iter_key.Valid(); ++iter_key) {
    split_macs->insert(make_pair(base::UTF16ToUTF8(iter_key.Name()),
                                 base::UTF16ToUTF8(iter_key.Value())));
  }

  return !split_macs->empty();
}

void RegistryHashStoreContentsWin::SetMac(const std::string& path,
                                          const std::string& value) {
  base::win::RegKey key;
  DCHECK_EQ(kMacSize, value.size());

  if (key.Create(HKEY_CURRENT_USER, preference_key_name_.c_str(),
                 KEY_SET_VALUE | KEY_WOW64_32KEY) == ERROR_SUCCESS) {
    key.WriteValue(base::UTF8ToUTF16(path).c_str(),
                   base::UTF8ToUTF16(value).c_str());
  }
}

void RegistryHashStoreContentsWin::SetSplitMac(const std::string& path,
                                               const std::string& split_path,
                                               const std::string& value) {
  base::win::RegKey key;
  DCHECK_EQ(kMacSize, value.size());

  if (key.Create(HKEY_CURRENT_USER,
                 GetSplitPrefKeyName(preference_key_name_, path).c_str(),
                 KEY_SET_VALUE | KEY_WOW64_32KEY) == ERROR_SUCCESS) {
    key.WriteValue(base::UTF8ToUTF16(split_path).c_str(),
                   base::UTF8ToUTF16(value).c_str());
  }
}

bool RegistryHashStoreContentsWin::RemoveEntry(const std::string& path) {
  // ClearSplitMac is first to avoid short-circuit issues.
  return ClearAtomicMac(preference_key_name_, path) ||
         ClearSplitMac(preference_key_name_, path);
}

void RegistryHashStoreContentsWin::ImportEntry(const std::string& path,
                                               const base::Value* in_value) {
  NOTREACHED()
      << "RegistryHashStore does not support the ImportEntry operation";
}

const base::DictionaryValue* RegistryHashStoreContentsWin::GetContents() const {
  NOTREACHED()
      << "RegistryHashStore does not support the GetContents operation";
  return NULL;
}

std::string RegistryHashStoreContentsWin::GetSuperMac() const {
  NOTREACHED()
      << "RegistryHashStore does not support the GetSuperMac operation";
  return NULL;
}

void RegistryHashStoreContentsWin::SetSuperMac(const std::string& super_mac) {
  NOTREACHED()
      << "RegistryHashStore does not support the SetSuperMac operation";
}
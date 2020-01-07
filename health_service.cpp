/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "android.hardware.health@2.0-service.stm32mp1"

#include <health2/Health.h>
#include <health2/service.h>
#include <healthd/healthd.h>

#include <android-base/file.h>

#include <android-base/logging.h>

using android::hardware::health::V2_0::DiskStats;
using android::hardware::health::V2_0::StorageInfo;

static constexpr char kBatteryStatsFile[] = "/sys/class/power_supply/";

static constexpr char kBatteryName[] = "dummy-battery";
static constexpr char kAcChargerName[] = "dummy-charger-ac";
static constexpr char kUsbChargerName[] = "dummy-charger-usb_c";

static constexpr size_t kDiskStatsSize = 11;

#ifdef EMMC_STORAGE
static constexpr char kDiskName[] = "mmcblk1";
static constexpr char kDiskStatsFile[] = "/sys/block/mmcblk1/stat";
#else
static constexpr char kDiskName[] = "mmcblk0";
static constexpr char kDiskStatsFile[] = "/sys/block/mmcblk0/stat";
#endif

void healthd_board_init(struct healthd_config*) {}

int healthd_board_battery_update(
    struct android::BatteryProperties* battery_props) {

  std::string buffer;
  std::string file, battery;

  battery = std::string(kBatteryStatsFile).append(std::string(kBatteryName)).append("/");

  // consider that the battery is always present
  battery_props->batteryPresent = true;

  file.assign(battery);
  file = file.append("technology");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }
  battery_props->batteryTechnology = buffer.c_str();

  file.assign(battery);
  file = file.append("capacity");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }
  battery_props->batteryLevel = std::stoi(buffer);

  file.assign(battery);
  file = file.append("current_max");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }
  battery_props->maxChargingCurrent = std::stoi(buffer);

  file.assign(battery);
  file = file.append("current_now");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }
  battery_props->batteryCurrent = std::stoi(buffer);

  file.assign(battery);
  file = file.append("voltage_max");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }
  battery_props->maxChargingVoltage = std::stoi(buffer);

  // read value in µV, returned value in mV
  file.assign(battery);
  file = file.append("voltage_now");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }
  battery_props->batteryVoltage = std::stoi(buffer) / 1000;

  file.assign(battery);
  file = file.append("temp");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }
  // read value in units of 0.1°C, returned value in °C
  battery_props->batteryTemperature = std::stoi(buffer) / 10;

  file.assign(battery);
  file = file.append("cycle_count");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }
  battery_props->batteryCycleCount = std::stoi(buffer);

  file.assign(battery);
  file = file.append("charge_full");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }
  battery_props->batteryFullCharge = std::stoi(buffer);

  // AC charger status
  file.assign(battery);
  file = file.append("charge_counter");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }
  battery_props->batteryChargeCounter = std::stoi(buffer);

  // AC charger status
  file = std::string(kBatteryStatsFile).append(std::string(kAcChargerName)).append("/online");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }
  battery_props->chargerAcOnline = buffer != "0";

  // USB charger status
  file = std::string(kBatteryStatsFile).append(std::string(kUsbChargerName)).append("/online");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }
  battery_props->chargerUsbOnline = buffer != "0";

  // No Wireless charger available
  battery_props->chargerWirelessOnline = false;

  // Batter status (full, charging, discharging, not-charging)
  file.assign(battery);
  file = file.append("status");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }

  // remove last space
  buffer.pop_back();
  if (buffer.compare("Charging") == 0) {
    battery_props->batteryStatus = android::BATTERY_STATUS_CHARGING;
  } else if (buffer.compare("Discharging") == 0) {
    battery_props->batteryStatus = android::BATTERY_STATUS_DISCHARGING;
  } else if (buffer.compare("Not-charging") == 0) {
    battery_props->batteryStatus = android::BATTERY_STATUS_NOT_CHARGING;
  } else if (buffer.compare("Full") == 0) {
    battery_props->batteryStatus = android::BATTERY_STATUS_FULL;
  } else {
    battery_props->batteryStatus = android::BATTERY_STATUS_UNKNOWN;
  }

  // Batter health (good, overheat, dead, overvoltage, failure)
  file.assign(battery);
  file = file.append("health");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return 1;
  }

  // remove last space
  buffer.pop_back();
  if (buffer.compare("Good") == 0) {
    battery_props->batteryHealth = android::BATTERY_HEALTH_GOOD;
  } else if (buffer.compare("Overheat") == 0) {
    battery_props->batteryHealth = android::BATTERY_HEALTH_OVERHEAT;
  } else if (buffer.compare("Dead") == 0) {
    battery_props->batteryHealth = android::BATTERY_HEALTH_DEAD;
  } else if (buffer.compare("Overvoltage") == 0) {
    battery_props->batteryHealth = android::BATTERY_HEALTH_OVER_VOLTAGE;
  } else if (buffer.compare("Failure") == 0) {
    battery_props->batteryHealth = android::BATTERY_HEALTH_UNSPECIFIED_FAILURE;
  } else {
    battery_props->batteryHealth = android::BATTERY_HEALTH_UNKNOWN;
  }

  LOG(DEBUG) << "chargerAcOnline = " << battery_props->chargerAcOnline << "\n"
            << "chargerUsbOnline = " << battery_props->chargerUsbOnline << "\n"
            << "chargerWirelessOnline = " << battery_props->chargerWirelessOnline << "\n"
            << "maxChargingCurrent = " << battery_props->maxChargingCurrent << "\n"
            << "maxChargingVoltage = " << battery_props->maxChargingVoltage << "\n"
            << "batteryStatus = " << battery_props->batteryStatus << "\n"
            << "batteryHealth = " << battery_props->batteryHealth << "\n"
            << "batteryPresent = " << battery_props->batteryPresent << "\n"
            << "batteryLevel = " << battery_props->batteryLevel << "\n"
            << "batteryVoltage = " << battery_props->batteryVoltage << "\n"
            << "batteryTemperature = " << battery_props->batteryTemperature << "\n"
            << "batteryCurrent = " << battery_props->batteryCurrent << "\n"
            << "batteryCycleCount = " << battery_props->batteryCycleCount << "\n"
            << "batteryFullCharge = " << battery_props->batteryFullCharge << "\n"
            << "batteryChargeCounter = " << battery_props->batteryChargeCounter << "\n"
            << "batteryTechnology = " << battery_props->batteryTechnology;

  return 0;
}

void get_storage_info(std::vector<struct StorageInfo>&) {}

void get_disk_stats(std::vector<struct DiskStats>& vec_stats) {
  DiskStats stats = {};

  stats.attr.isInternal = true;
  stats.attr.isBootDevice = true;
  stats.attr.name = std::string(kDiskName);

  std::string buffer;
  if (!android::base::ReadFileToString(std::string(kDiskStatsFile), &buffer)) {
    LOG(ERROR) << kDiskStatsFile << ": ReadFileToString failed.";
    return;
  }

  // Regular diskstats entries
  std::stringstream ss(buffer);
  for (uint i = 0; i < kDiskStatsSize; i++) {
    ss >> *(reinterpret_cast<uint64_t*>(&stats) + i);
  }
  vec_stats.resize(1);
  vec_stats[0] = stats;

  return;
}

int main(void) { return health_service_main(); }

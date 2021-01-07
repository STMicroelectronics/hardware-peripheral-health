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

#define LOG_TAG "android.hardware.health@2.1-impl.stm32mp1"

#include <memory>
#include <string_view>

#include <android-base/logging.h>
#include <android-base/file.h>

#include <health/utils.h>
#include <health2impl/Health.h>
#include <hidl/Status.h>

#include <hal_conversion.h>

using ::android::sp;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::health::InitHealthdConfig;
using ::android::hardware::health::V2_1::IHealth;
using ::android::hidl::base::V1_0::IBase;
using ::android::hardware::health::V2_0::DiskStats;
using ::android::hardware::health::V2_0::Result;

using ::android::hardware::health::V1_0::hal_conversion::convertFromHealthInfo;
using ::android::hardware::health::V1_0::hal_conversion::convertToHealthInfo;

using namespace std::literals;

namespace android {
namespace hardware {
namespace health {
namespace V2_1 {
namespace implementation {

static constexpr char kBatteryStatsFile[] = "/sys/class/power_supply/";

static constexpr char kBatteryName[] = "dummy-battery";
static constexpr char kAcChargerName[] = "dummy-charger-ac";
static constexpr char kUsbChargerName[] = "dummy-charger-usb_c";

static constexpr size_t kDiskStatsSize = 11;

#ifdef EMMC_STORAGE
static constexpr char kBootDeviceName[] = "mmcblk1";
static constexpr char kBootDeviceStatsFile[] = "/sys/block/mmcblk1/stat";
#else
static constexpr char kBootDeviceName[] = "mmcblk0";
static constexpr char kBootDeviceStatsFile[] = "/sys/block/mmcblk0/stat";
#endif

// android::hardware::health::V2_1::implementation::Health implements most
// defaults.
class HealthImpl : public Health {
  public:
    HealthImpl(std::unique_ptr<healthd_config>&& config)
        : Health(std::move(config)) {}
    // Return<void> getChargeCounter(getChargeCounter_cb _hidl_cb) override;
    // Return<void> getCurrentNow(getCurrentNow_cb _hidl_cb) override;
    // Return<void> getCurrentAverage(getCurrentAverage_cb _hidl_cb) override;
    // Return<void> getCapacity(getCapacity_cb _hidl_cb) override;
    // Return<void> getEnergyCounter(getEnergyCounter_cb _hidl_cb) override;
    // Return<void> getChargeStatus(getChargeStatus_cb _hidl_cb) override;
    // Return<void> getStorageInfo(getStorageInfo_cb _hidl_cb) override;
    Return<void> getDiskStats(getDiskStats_cb _hidl_cb) override;
    // Return<void> getHealthInfo(getHealthInfo_cb _hidl_cb) override;

    // Functions introduced in Health HAL 2.1.
    // Return<void> getHealthConfig(getHealthConfig_cb _hidl_cb) override;
    // Return<void> getHealthInfo_2_1(getHealthInfo_2_1_cb _hidl_cb) override;
    // Return<void> shouldKeepScreenOn(shouldKeepScreenOn_cb _hidl_cb) override;

  protected:
    void UpdateHealthInfo(HealthInfo* health_info) override;

  private:
    bool BoardBatteryUpdate(BatteryProperties* battery_props);
};

void HealthImpl::UpdateHealthInfo(HealthInfo* health_info) {
  struct BatteryProperties props;

  convertFromHealthInfo(health_info->legacy.legacy, &props);
  bool res = BoardBatteryUpdate(&props);
  if (res)
    convertToHealthInfo(&props, health_info->legacy.legacy);
}

bool HealthImpl::BoardBatteryUpdate(BatteryProperties* battery_props) {

  std::string buffer;
  std::string file, battery;

  battery = std::string(kBatteryStatsFile).append(std::string(kBatteryName)).append("/");

  // consider that the battery is always present
  battery_props->batteryPresent = true;

  file.assign(battery);
  file = file.append("technology");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return false;
  }
  battery_props->batteryTechnology = buffer.c_str();

  file.assign(battery);
  file = file.append("capacity");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return false;
  }
  battery_props->batteryLevel = std::stoi(buffer);

  file.assign(battery);
  file = file.append("current_max");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return false;
  }
  battery_props->maxChargingCurrent = std::stoi(buffer);

  file.assign(battery);
  file = file.append("current_now");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return false;
  }
  battery_props->batteryCurrent = std::stoi(buffer);

  file.assign(battery);
  file = file.append("voltage_max");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return false;
  }
  battery_props->maxChargingVoltage = std::stoi(buffer);

  // read value in µV, returned value in mV
  file.assign(battery);
  file = file.append("voltage_now");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return false;
  }
  battery_props->batteryVoltage = std::stoi(buffer) / 1000;

  file.assign(battery);
  file = file.append("temp");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return false;
  }
  // read value in units of 0.1°C, returned value in °C
  battery_props->batteryTemperature = std::stoi(buffer) / 10;

  file.assign(battery);
  file = file.append("cycle_count");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return false;
  }
  battery_props->batteryCycleCount = std::stoi(buffer);

  file.assign(battery);
  file = file.append("charge_full");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return false;
  }
  battery_props->batteryFullCharge = std::stoi(buffer);

  // AC charger status
  file.assign(battery);
  file = file.append("charge_counter");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return false;
  }
  battery_props->batteryChargeCounter = std::stoi(buffer);

  // AC charger status
  file = std::string(kBatteryStatsFile).append(std::string(kAcChargerName)).append("/online");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return false;
  }
  battery_props->chargerAcOnline = buffer != "0";

  // USB charger status
  file = std::string(kBatteryStatsFile).append(std::string(kUsbChargerName)).append("/online");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return false;
  }
  battery_props->chargerUsbOnline = buffer != "0";

  // No Wireless charger available
  battery_props->chargerWirelessOnline = false;

  // Batter status (full, charging, discharging, not-charging)
  file.assign(battery);
  file = file.append("status");
  if (!android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return false;
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
    return false;
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

  return true;
}

Return<void> HealthImpl::getDiskStats(getDiskStats_cb _hidl_cb) {
  std::vector<struct DiskStats> stats;

  // Integrate only the boot device stats by default
  DiskStats bootDeviceStat = {};

  bootDeviceStat.attr.isInternal = true;
  bootDeviceStat.attr.isBootDevice = true;
  bootDeviceStat.attr.name = std::string(kBootDeviceName);

  std::string buffer;
  if (!android::base::ReadFileToString(std::string(kBootDeviceStatsFile), &buffer)) {
    LOG(ERROR) << kBootDeviceStatsFile << ": ReadFileToString failed.";
    _hidl_cb(Result::NOT_SUPPORTED, {});
    return Void();
  }

  std::stringstream ss(buffer);
  for (uint i = 0; i < kDiskStatsSize; i++) {
    ss >> *(reinterpret_cast<uint64_t*>(&bootDeviceStat) + i);
  }

  stats.resize(1);
  stats[0] = bootDeviceStat;

  hidl_vec<struct DiskStats> stats_vec(stats);
  if (!stats.size()) {
    _hidl_cb(Result::NOT_SUPPORTED, {});
  } else {
    _hidl_cb(Result::SUCCESS, stats_vec);
  }
  return Void();
}

}  // namespace implementation
}  // namespace V2_1
}  // namespace health
}  // namespace hardware
}  // namespace android

extern "C" IHealth* HIDL_FETCH_IHealth(const char* instance) {
    using ::android::hardware::health::V2_1::implementation::HealthImpl;
    if (instance != "default"sv) {
        return nullptr;
    }
    auto config = std::make_unique<healthd_config>();
    InitHealthdConfig(config.get());

    return new HealthImpl(std::move(config));
}

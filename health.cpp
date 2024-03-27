/*
 * Copyright (C) 2022 The Android Open Source Project
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
#define LOG_TAG "android.hardware.health-service.stm32mpu"

#include <memory>
#include <string_view>

#include <android-base/logging.h>
#include <android-base/file.h>
#include <android/binder_interface_utils.h>
#include <health-impl/Health.h>
#include <health/utils.h>

using ::aidl::android::hardware::health::BatteryHealth;
using ::aidl::android::hardware::health::BatteryStatus;
using ::aidl::android::hardware::health::HalHealthLoop;
using ::aidl::android::hardware::health::Health;
using ::aidl::android::hardware::health::HealthInfo;
using ::aidl::android::hardware::health::IHealth;
using ::android::hardware::health::InitHealthdConfig;
using ::ndk::ScopedAStatus;
using ::ndk::SharedRefBase;
using namespace std::literals;

namespace aidl::android::hardware::health {

// Health HAL implementation for STM32MPU. Note that in this implementation,
// It pretends to be a device with a battery being charged, using the
// dummy-battery driver.

class HealthImpl : public Health {
 public:
  // Inherit constructor
  using Health::Health;
  virtual ~HealthImpl() {}

  ScopedAStatus getChargeCounterUah(int32_t* out) override;
  ScopedAStatus getCurrentNowMicroamps(int32_t* out) override;
  ScopedAStatus getCurrentAverageMicroamps(int32_t* out) override;
  ScopedAStatus getCapacity(int32_t* out) override;
  ScopedAStatus getChargeStatus(BatteryStatus* out) override;
  ScopedAStatus getDiskStats(std::vector<DiskStats>* out) override;

 protected:
  void UpdateHealthInfo(HealthInfo* health_info) override;
};

static constexpr char kBatteryStatsFile[] = "/sys/class/power_supply/";

static constexpr char kBatteryName[] = "dummy-battery";
static constexpr char kAcChargerName[] = "dummy-charger-ac";
static constexpr char kUsbChargerName[] = "dummy-charger-usb_c";

void HealthImpl::UpdateHealthInfo(HealthInfo* health_info) {

  std::string buffer;
  std::string file, battery;

  battery = std::string(kBatteryStatsFile).append(std::string(kBatteryName)).append("/");

  // AC charger status
  file = std::string(kBatteryStatsFile).append(std::string(kAcChargerName)).append("/online");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->chargerAcOnline = false;
  } else {
    health_info->chargerAcOnline = buffer != "0";
  }

  // USB charger status
  file = std::string(kBatteryStatsFile).append(std::string(kUsbChargerName)).append("/online");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->chargerUsbOnline = false;
  } else {
    health_info->chargerUsbOnline = buffer != "0";
  }

  // No Wireless charger available
  health_info->chargerWirelessOnline = false;

  // Charging current not available, stub
  file.assign(battery);
  file = file.append("current_max");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->batteryStatus = BatteryStatus::UNKNOWN;
    health_info->batteryHealth = BatteryHealth::UNKNOWN;
    return;
  }
  health_info->maxChargingCurrentMicroamps = std::stoi(buffer);

  file.assign(battery);
  file = file.append("voltage_max");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->batteryStatus = BatteryStatus::UNKNOWN;
    health_info->batteryHealth = BatteryHealth::UNKNOWN;
    return;
  }
  health_info->maxChargingVoltageMicrovolts = std::stoi(buffer);

  // Battery status (full, charging, discharging, not-charging)
  file.assign(battery);
  file = file.append("status");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->batteryStatus = BatteryStatus::UNKNOWN;
    health_info->batteryHealth = BatteryHealth::UNKNOWN;
    return;
  }

  buffer.pop_back();
  if (buffer.compare("Charging") == 0) {
    health_info->batteryStatus = BatteryStatus::CHARGING;
  } else if (buffer.compare("Discharging") == 0) {
    health_info->batteryStatus = BatteryStatus::DISCHARGING;
  } else if (buffer.compare("Not-charging") == 0) {
    health_info->batteryStatus = BatteryStatus::NOT_CHARGING;
  } else if (buffer.compare("Full") == 0) {
    health_info->batteryStatus = BatteryStatus::FULL;
  } else {
    health_info->batteryStatus = BatteryStatus::UNKNOWN;
  }

  // Batter health (good, overheat, dead, overvoltage, failure)
  file.assign(battery);
  file = file.append("health");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->batteryStatus = BatteryStatus::UNKNOWN;
    health_info->batteryHealth = BatteryHealth::UNKNOWN;
    return;
  }

  buffer.pop_back();
  if (buffer.compare("Good") == 0) {
    health_info->batteryHealth = BatteryHealth::GOOD;
  } else if (buffer.compare("Overheat") == 0) {
    health_info->batteryHealth = BatteryHealth::OVERHEAT;
  } else if (buffer.compare("Dead") == 0) {
    health_info->batteryHealth = BatteryHealth::DEAD;
  } else if (buffer.compare("Overvoltage") == 0) {
    health_info->batteryHealth = BatteryHealth::OVER_VOLTAGE;
  } else if (buffer.compare("Failure") == 0) {
    health_info->batteryHealth = BatteryHealth::UNSPECIFIED_FAILURE;
  } else {
    health_info->batteryHealth = BatteryHealth::UNKNOWN;
  }

  // consider that the battery is always present
  health_info->batteryPresent = true;

  file.assign(battery);
  file = file.append("capacity");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->batteryStatus = BatteryStatus::UNKNOWN;
    health_info->batteryHealth = BatteryHealth::UNKNOWN;
    return;
  }
  health_info->batteryLevel = std::stoi(buffer);

  // read value in µV, return value in mV
  file.assign(battery);
  file = file.append("voltage_now");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->batteryStatus = BatteryStatus::UNKNOWN;
    health_info->batteryHealth = BatteryHealth::UNKNOWN;
    return;
  }
  health_info->batteryVoltageMillivolts = std::stoi(buffer) / 1000;

  file.assign(battery);
  file = file.append("temp");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->batteryStatus = BatteryStatus::UNKNOWN;
    health_info->batteryHealth = BatteryHealth::UNKNOWN;
    return;
  }
  // read value in units of 0.1°C, return value as it is
  health_info->batteryTemperatureTenthsCelsius = std::stoi(buffer);

  file.assign(battery);
  file = file.append("current_now");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->batteryStatus = BatteryStatus::UNKNOWN;
    health_info->batteryHealth = BatteryHealth::UNKNOWN;
    return;
  }
  // read value in units of microamps, return value as it is
  health_info->batteryCurrentMicroamps = std::stoi(buffer);

  file.assign(battery);
  file = file.append("cycle_count");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->batteryStatus = BatteryStatus::UNKNOWN;
    health_info->batteryHealth = BatteryHealth::UNKNOWN;
    return;
  }
  health_info->batteryCycleCount = std::stoi(buffer);

  file.assign(battery);
  file = file.append("charge_full");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->batteryStatus = BatteryStatus::UNKNOWN;
    health_info->batteryHealth = BatteryHealth::UNKNOWN;
    return;
  }
  // read value in units of microamps-hour, return value as it is
  health_info->batteryFullChargeUah = std::stoi(buffer);

  file.assign(battery);
  file = file.append("charge_counter");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->batteryStatus = BatteryStatus::UNKNOWN;
    health_info->batteryHealth = BatteryHealth::UNKNOWN;
    return;
  }
  health_info->batteryChargeCounterUah = std::stoi(buffer);

  file.assign(battery);
  file = file.append("technology");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    health_info->batteryStatus = BatteryStatus::UNKNOWN;
    health_info->batteryHealth = BatteryHealth::UNKNOWN;
    return;
  }
  health_info->batteryTechnology = buffer.c_str();
}

ScopedAStatus HealthImpl::getChargeCounterUah(int32_t* out) {
  std::string buffer;
  std::string file, battery;

  battery = std::string(kBatteryStatsFile).append(std::string(kBatteryName)).append("/");

  file.assign(battery);
  file = file.append("charge_counter");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
  }
  *out = std::stoi(buffer);

  return ScopedAStatus::ok();
}

ScopedAStatus HealthImpl::getCurrentNowMicroamps(int32_t* out) {
  std::string buffer;
  std::string file, battery;

  battery = std::string(kBatteryStatsFile).append(std::string(kBatteryName)).append("/");

  file.assign(battery);
  file = file.append("current_now");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
  }
  *out = std::stoi(buffer);
  return ScopedAStatus::ok();
}

ScopedAStatus HealthImpl::getCurrentAverageMicroamps(int32_t* out) {
  std::string buffer;
  std::string file, battery;

  battery = std::string(kBatteryStatsFile).append(std::string(kBatteryName)).append("/");

  file.assign(battery);
  file = file.append("current_avg");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
  }
  *out = std::stoi(buffer);
  return ScopedAStatus::ok();
}

ScopedAStatus HealthImpl::getCapacity(int32_t* out) {
  std::string buffer;
  std::string file, battery;

  battery = std::string(kBatteryStatsFile).append(std::string(kBatteryName)).append("/");

  file.assign(battery);
  file = file.append("capacity");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
  }
  *out = std::stoi(buffer);
  return ScopedAStatus::ok();
}

ScopedAStatus HealthImpl::getChargeStatus(BatteryStatus* out) {
  std::string buffer;
  std::string file, battery;

  battery = std::string(kBatteryStatsFile).append(std::string(kBatteryName)).append("/");

  file.assign(battery);
  file = file.append("status");
  if (!::android::base::ReadFileToString(file, &buffer)) {
    LOG(ERROR) << file << ": ReadFileToString failed.";
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
  }

  buffer.pop_back();
  if (buffer.compare("Charging") == 0) {
    *out = BatteryStatus::CHARGING;
  } else if (buffer.compare("Discharging") == 0) {
    *out = BatteryStatus::DISCHARGING;
  } else if (buffer.compare("Not-charging") == 0) {
    *out = BatteryStatus::NOT_CHARGING;
  } else if (buffer.compare("Full") == 0) {
    *out = BatteryStatus::FULL;
  } else {
    *out = BatteryStatus::UNKNOWN;
  }
  
  return ScopedAStatus::ok();
}

static constexpr size_t kDiskStatsSize = 11;

#ifdef EMMC_STORAGE
static constexpr char kBootDeviceStatsFile[] = "/sys/block/mmcblk1/stat";
#else
static constexpr char kBootDeviceStatsFile[] = "/sys/block/mmcblk0/stat";
#endif

ScopedAStatus HealthImpl::getDiskStats(std::vector<DiskStats>* out) {

  std::vector<DiskStats> stats;

  // Integrate only the boot device stats (eMMC or microSD)
  DiskStats bootDeviceStat = {};

  std::string buffer;
  if (!::android::base::ReadFileToString(std::string(kBootDeviceStatsFile), &buffer)) {
    LOG(ERROR) << kBootDeviceStatsFile << ": ReadFileToString failed.";
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
  }

  std::stringstream ss(buffer);
  for (uint i = 0; i < kDiskStatsSize; i++) {
    ss >> *(reinterpret_cast<uint64_t*>(&bootDeviceStat) + i);
  }

  stats.resize(1);
  stats[0] = bootDeviceStat;

  *out = stats;

  return ScopedAStatus::ok();
}


}  // namespace aidl::android::hardware::health

int main(int, [[maybe_unused]] char** argv) {
#ifdef __ANDROID_RECOVERY__
  android::base::InitLogging(argv, android::base::KernelLogger);
#endif
  // STM32MPU does not handle --charger option.
  using aidl::android::hardware::health::HealthImpl;
  LOG(INFO) << "Starting health HAL.";
  auto config = std::make_unique<healthd_config>();
  InitHealthdConfig(config.get());
  auto binder = SharedRefBase::make<HealthImpl>("default", std::move(config));
  auto hal_health_loop = std::make_shared<HalHealthLoop>(binder, binder);
  return hal_health_loop->StartLoop();
}

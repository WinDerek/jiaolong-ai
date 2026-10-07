#include "health/mac_system_health_data_collector.h"

#include <cstdint>
#include <cstring>
#include <string>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>

namespace mac_system_health {

namespace {

// Selectors and constants of the AppleSMC user-space interface.
constexpr int kKernelIndexSMC = 2;
constexpr int kSmcCommandReadBytes = 5;
constexpr int kSmcCommandReadKeyInfo = 9;

// Candidate SMC keys for the CPU temperature, tried in order.
const char* const kCpuTemperatureKeys[] = {
    "TC0P", "TC0D", "TC0H", "TC0E", "Tp09", "Tp0A", "Tp0B", "Tp0C",
    "Tp0D", "Tp0E", "Tp0F", "Tp10", "Tp11", "Tp1C"};

// Mirrors the structs exchanged with the AppleSMC kernel driver through
// IOConnectCallStructMethod(). The layout is fixed by the SMC user-space
// interface.
typedef struct {
  char major;
  char minor;
  char build;
  char reserved[1];
  UInt16 release;
} SMCKeyDataVers;

typedef struct {
  UInt16 version;
  UInt16 length;
  UInt32 cpuPLimit;
  UInt32 gpuPLimit;
  UInt32 memPLimit;
} SMCKeyDataPLimitData;

typedef struct {
  UInt32 dataSize;
  UInt32 dataType;
  char dataAttributes;
} SMCKeyDataKeyInfo;

typedef char SMCBytes[32];

typedef struct {
  UInt32 key;
  SMCKeyDataVers vers;
  SMCKeyDataPLimitData pLimitData;
  SMCKeyDataKeyInfo keyInfo;
  char result;
  char status;
  char data8;
  UInt32 data32;
  SMCBytes bytes;
} SMCKeyData;

// Converts a 4-character SMC key (e.g. "TC0P") into the big-endian UInt32
// representation used by the SMC interface.
UInt32 SmcKeyToFourCC(const std::string& key) {
  UInt32 value = 0;
  for (int i = 0; i < 4 && i < static_cast<int>(key.size()); ++i) {
    value = (value << 8) | static_cast<unsigned char>(key[i]);
  }
  return value;
}

}  // namespace

MacSystemHealthDataCollector::MacSystemHealthDataCollector() : connection_(0) {
  Open();
}

MacSystemHealthDataCollector::~MacSystemHealthDataCollector() {
  Close();
}

bool MacSystemHealthDataCollector::Open() {
  Close();

  CFMutableDictionaryRef matching = IOServiceMatching("AppleSMC");
  if (matching == nullptr) {
    return false;
  }
  // IOServiceGetMatchingService consumes the matching dictionary, so it must
  // not be released here.
#if defined(kIOMainPortDefault)
  io_service_t service =
      IOServiceGetMatchingService(kIOMainPortDefault, matching);
#else
  io_service_t service =
      IOServiceGetMatchingService(kIOMasterPortDefault, matching);
#endif
  if (service == 0) {
    return false;
  }

  const kern_return_t result =
      IOServiceOpen(service, mach_task_self(), 0, &connection_);
  IOObjectRelease(service);
  if (result != kIOReturnSuccess) {
    connection_ = 0;
    return false;
  }
  return true;
}

void MacSystemHealthDataCollector::Close() {
  if (connection_ != 0) {
    IOServiceClose(connection_);
    connection_ = 0;
  }
}

bool MacSystemHealthDataCollector::ReadKeyData(const std::string& key,
                                              unsigned char bytes[32]) {
  if (connection_ == 0 || key.size() != 4) {
    return false;
  }

  const UInt32 four_cc = SmcKeyToFourCC(key);
  SMCKeyData input = {};
  SMCKeyData output = {};
  size_t output_size = sizeof(SMCKeyData);

  // First ask the SMC for the key's metadata (payload size etc.).
  input.key = four_cc;
  input.data8 = kSmcCommandReadKeyInfo;
  kern_return_t result = IOConnectCallStructMethod(
      connection_, kKernelIndexSMC, &input, sizeof(SMCKeyData), &output,
      &output_size);
  if (result != kIOReturnSuccess) {
    return false;
  }

  // Then read the key's payload bytes.
  input = {};
  input.key = four_cc;
  input.keyInfo.dataSize = output.keyInfo.dataSize;
  input.data8 = kSmcCommandReadBytes;
  output_size = sizeof(SMCKeyData);
  result = IOConnectCallStructMethod(connection_, kKernelIndexSMC, &input,
                                     sizeof(SMCKeyData), &output, &output_size);
  if (result != kIOReturnSuccess) {
    return false;
  }

  std::memcpy(bytes, output.bytes, 32);
  return true;
}

std::optional<double> MacSystemHealthDataCollector::DecodeTemperature(
    const unsigned char bytes[32]) {
  // SMC temperature values use the "sp" fixed-point format: bytes[0..1] are
  // the type marker ('s''p'), bytes[2] is the payload length (2 for the 8.8
  // sp78 format, 4 for the 16.16 sp84 format), and the big-endian value starts
  // at byte 4.
  if (bytes[0] != 's' || bytes[1] != 'p') {
    return std::nullopt;
  }

  const int length = bytes[2];
  if (length == 2) {
    const std::int16_t raw = static_cast<std::int16_t>(
        (static_cast<std::uint16_t>(bytes[4]) << 8) |
        static_cast<std::uint16_t>(bytes[5]));
    return static_cast<double>(raw) / 256.0;
  }
  if (length == 4) {
    const std::int32_t raw =
        (static_cast<std::int32_t>(bytes[4]) << 24) |
        (static_cast<std::int32_t>(bytes[5]) << 16) |
        (static_cast<std::int32_t>(bytes[6]) << 8) |
        static_cast<std::int32_t>(bytes[7]);
    return static_cast<double>(raw) / 65536.0;
  }
  return std::nullopt;
}

std::optional<double> MacSystemHealthDataCollector::ReadCpuTemperatureCelsius() {
  constexpr size_t kKeyCount =
      sizeof(kCpuTemperatureKeys) / sizeof(kCpuTemperatureKeys[0]);
  for (size_t i = 0; i < kKeyCount; ++i) {
    unsigned char bytes[32] = {};
    if (!ReadKeyData(kCpuTemperatureKeys[i], bytes)) {
      continue;
    }
    std::optional<double> temperature = DecodeTemperature(bytes);
    if (temperature.has_value()) {
      return temperature;
    }
  }
  return std::nullopt;
}

}  // namespace mac_system_health
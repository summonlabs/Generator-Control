// Generator Control - fuel and consumable resource evidence.
//
// Resource evidence is never inferred. A tank gauge that cannot be read produces
// Missing, an interface that cannot report a level produces Unsupported, and a
// gauge whose last reading is older than its freshness bound produces Stale. None
// of those become "zero fuel" and none of them become permission.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "genctl/digest.hpp"
#include "genctl/evidence.hpp"
#include "genctl/ids.hpp"
#include "genctl/state.hpp"
#include "genctl/units.hpp"

namespace genctl {

enum class ResourceKind : std::uint8_t {
  Unknown = 0,
  FuelLevel = 1,
  FuelCapacity = 2,
  FuelConsumptionRate = 3,
  FuelTemperature = 4,
  LubeOilPressure = 5,
  LubeOilTemperature = 6,
  CoolantTemperature = 7,
  CoolantLevel = 8,
  StartingBatteryVoltage = 9,
  CompressedAirPressure = 10,
};

[[nodiscard]] std::string_view to_string(ResourceKind kind) noexcept;

// Everything the runtime is allowed to know about a generator's consumables.
struct ResourceSnapshot {
  Evidence<FuelQuantity> fuel_level{};
  Evidence<FuelQuantity> fuel_capacity{};
  Evidence<FuelRate> fuel_consumption{};
  Evidence<Temperature> fuel_temperature{};
  Evidence<Pressure> lube_oil_pressure{};
  Evidence<Temperature> lube_oil_temperature{};
  Evidence<Temperature> coolant_temperature{};
  Evidence<Percent> coolant_level{};
  Evidence<Voltage> battery_voltage{};
  Evidence<Pressure> compressed_air_pressure{};
};

// What an operation needs from the consumables. Values come from installation
// configuration, never from evidence.
struct ResourceRequirement {
  // Fuel that must remain untouched: the runtime may not plan to consume it.
  FuelQuantity reserve{};
  // Runtime that must be available at the observed consumption rate. Zero means
  // "any positive runtime is sufficient"; a negative value means "runtime is not
  // part of this requirement".
  std::int64_t required_runtime_seconds{0};
  bool require_lube_oil_pressure{true};
  bool require_coolant_temperature{true};
  bool require_battery_voltage{false};
  bool require_compressed_air{false};
  // Freshness bounds applied to dynamic resource observations.
  Millis fuel_max_age_millis{15 * kMillisPerMinute};
  Millis machinery_max_age_millis{60 * kMillisPerSecond};
};

struct ResourceFinding {
  ResourceKind kind{ResourceKind::Unknown};
  EvidenceUsability usability{EvidenceUsability::Unknown};
  ErrorCode code{ErrorCode::Ok};
  EvidenceSource source{EvidenceSource::Unknown};
  bool required{true};
  std::string detail{};
};

struct ResourceAssessment {
  GeneratorId generator{};
  StateRevision revision{};
  ControllerGeneration controller{};
  EpochMillis evaluated_at{0};
  bool sufficient{false};
  ErrorCode primary_error{ErrorCode::Ok};
  ValidationStage stage{ValidationStage::None};
  RuntimeEstimate runtime{};
  bool synthetic_evidence_used{false};
  std::vector<ResourceFinding> findings{};
  Digest256 binding_digest{};

  [[nodiscard]] bool ok() const noexcept { return sufficient; }
};

[[nodiscard]] Status assess_resources(const GeneratorId& generator, StateRevision revision,
                                      const ControllerGeneration& controller,
                                      const ResourceSnapshot& snapshot,
                                      const ResourceRequirement& requirement, EpochMillis now,
                                      const EvidencePolicy& policy, bool waive_advisory,
                                      ResourceAssessment* out);

// True when the supplied evidence carries a value that can take part in arithmetic
// for the given unit; mismatched units are refused rather than converted.
[[nodiscard]] bool unit_compatible(FuelUnit a, FuelUnit b) noexcept;

}  // namespace genctl

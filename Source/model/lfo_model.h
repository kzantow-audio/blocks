#include "module_new.h"
#include "vital/synthesis/modules/lfo_module.h"
#include "vital/synth_strings.h"

namespace model {
class LFOModule: public Module {
public:
  LFOModule(int number): Module("lfo", number) {
    add({ .name = "wave", .min = 0.0, .max = 4.0, .value_scale = ValueScale::kIndexed, .string_lookup = strings::modulator_waves, .modulatable = false });
    add({ .name = "tempo", .min = -7.0, .max = 12.0 }); 
    add({ .name = "sync", .min = 0.0, .max = 4.0, .value_scale = ValueScale::kIndexed, .string_lookup = strings::kFrequencySyncNames, .modulatable = false });
    add({ .name = "mode", .min = 0.0, .max = 5.0, .value_scale = ValueScale::kIndexed, .string_lookup = strings::kSyncNames, .modulatable = false  });
    add({ .name = "frequency", .min = -7.0, .max = 9.0, .value_scale = ValueScale::kExponential, .display_invert = true, .display_name = "secs", .decimal_places = 3, .hidden = true });
  }

  std::string getParameterName(std::string name) override {
    if (name == "tempo") {
      bool is_seconds_mode = parameter_map_["sync"]->value_processor->value() == 0.0f;
      return is_seconds_mode ? "frequency" : "tempo";
    }
    return Module::getParameterName(name);
  };
};
}

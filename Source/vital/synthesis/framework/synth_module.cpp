/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "vital/synthesis/framework/synth_module.h"

#include "vital/common/synth_constants.h"
#include "vital/synthesis/utilities/smooth_value.h"
#include "vital/synthesis/utilities/value_switch.h"
#include "vital/synthesis/modulators/trigger_random.h" 

namespace vital {
std::string removeFirstTwoSections(const std::string& input) {
  size_t firstUnderscore = input.find('_');
  if (firstUnderscore == std::string::npos) {
    // No underscores found, return the original string
    return input;
  }

  size_t secondUnderscore = input.find('_', firstUnderscore + 1);
  if (secondUnderscore == std::string::npos) {
    // Only one underscore found, return the substring after it
    return input.substr(firstUnderscore + 1);
  }

  // Return the substring after the second underscore
  return input.substr(secondUnderscore + 1);
}

Value* SynthModule::createBaseControl(std::string name, bool audio_rate, bool smooth_value) {
  mono_float default_value = Parameters::getDetails(name).default_value;

  Value* val = nullptr;
  if (audio_rate) {
    if (smooth_value) {
      val = new SmoothValue(default_value);
      addMonoProcessor(val, false);
    } else {
      val = new Value(default_value);
      addIdleMonoProcessor(val);
    }
  } else {
    if (smooth_value) {
      val = new cr::SmoothValue(default_value);
      addMonoProcessor(val, false);
    } else {
      val = new cr::Value(default_value);
      addIdleMonoProcessor(val);
    }
  }
  data_->controls[name] = val;
  auto adjusted_name = removeFirstTwoSections(name);
  control_map_[adjusted_name] = val;
  return val;
}

Output* SynthModule::createBaseModControl(std::string name, bool audio_rate, bool smooth_value, Output* internal_modulation) {
  Processor* base_val = createBaseControl(name, audio_rate, smooth_value);

  Processor* mono_total = nullptr;
  if (audio_rate)
    mono_total = new ModulationSum();
  else
    mono_total = new cr::VariableAdd();

  mono_total->plugNext(base_val);
  addMonoProcessor(mono_total, false);
  data_->mono_mod_destinations[name] = mono_total;
  data_->mono_modulation_readout[name] = mono_total->output();

  ValueSwitch* control_switch = new ValueSwitch(0.0f);
  control_switch->plugNext(base_val);
  control_switch->plugNext(mono_total);

  if (internal_modulation)
    mono_total->plugNext(internal_modulation);
  else
    control_switch->addProcessor(mono_total);

  addIdleMonoProcessor(control_switch);
  if (smooth_value || internal_modulation)
    control_switch->set(1);
  else
    control_switch->set(0);
  data_->mono_modulation_switches[name] = control_switch;

  return control_switch->output(ValueSwitch::kSwitch);
}

Output* SynthModule::createMonoModControl(std::string name, bool audio_rate, bool smooth_value, Output* internal_modulation) {
  ValueDetails details = Parameters::getDetails(name);
  Output* control_rate_total = createBaseModControl(name, audio_rate, smooth_value, internal_modulation);
  if (audio_rate)
    return control_rate_total;

  if (details.value_scale == ValueDetails::kQuadratic) {
    Processor* scale = nullptr;
    if (details.post_offset)
      scale = new cr::Quadratic(details.post_offset);
    else
      scale = new cr::Square();

    scale->plug(control_rate_total);
    addMonoProcessor(scale);
    control_rate_total = scale->output();
  } else if (details.value_scale == ValueDetails::kCubic) {
    Processor* scale = nullptr;
    VITAL_ASSERT(details.post_offset == 0.0f);
    if (details.post_offset)
      scale = new cr::Cubic(details.post_offset);
    else
      scale = new cr::Cube();

    scale->plug(control_rate_total);
    addMonoProcessor(scale);
    control_rate_total = scale->output();
  } else if (details.value_scale == ValueDetails::kQuartic) {
    Processor* scale = nullptr;
    VITAL_ASSERT(details.post_offset == 0.0f);
    if (details.post_offset)
      scale = new cr::Quartic(details.post_offset);
    else
      scale = new cr::Quart();

    scale->plug(control_rate_total);
    addMonoProcessor(scale);
    control_rate_total = scale->output();
  } else if (details.value_scale == ValueDetails::kExponential) {
    cr::ExponentialScale* exponential = new cr::ExponentialScale(details.min, details.max, 2.0f);
    exponential->plug(control_rate_total);
    addMonoProcessor(exponential);
    control_rate_total = exponential->output();
  } else if (details.value_scale == ValueDetails::kSquareRoot) {
    cr::Root* root = new cr::Root(details.post_offset);
    root->plug(control_rate_total);
    addMonoProcessor(root);
    control_rate_total = root->output();
  }

  return control_rate_total;
}

Output* SynthModule::createPolyModControl(std::string name, bool audio_rate, bool smooth_value, Output* internal_modulation, Input* reset) {
  ValueDetails details = Parameters::getDetails(name);
  Output* base_control = createBaseModControl(name, audio_rate, smooth_value);

  Processor* poly_total;
  if (audio_rate) {
    poly_total = new ModulationSum();
    if (reset)
      poly_total->useInput(reset, ModulationSum::kReset);
  } else
    poly_total = new cr::VariableAdd();
  addProcessor(poly_total);
  data_->poly_mod_destinations[name] = poly_total;

  Processor* modulation_total;
  if (audio_rate)
    modulation_total = new Add();
  else
    modulation_total = new cr::Add();

  modulation_total->plug(base_control, 0);
  modulation_total->plug(poly_total, 1);
  addProcessor(modulation_total);

  data_->poly_modulation_readout[name] = poly_total->output();

  ValueSwitch* control_switch = new ValueSwitch(0.0f);
  control_switch->plugNext(base_control);
  control_switch->plugNext(modulation_total);

  if (internal_modulation) {
    poly_total->plugNext(internal_modulation);
    control_switch->set(1);
  } else {
    control_switch->addProcessor(poly_total);
    control_switch->addProcessor(modulation_total);
    control_switch->set(0);
  }
  addIdleProcessor(control_switch);
  data_->poly_modulation_switches[name] = control_switch;

  Output* control_rate_total = control_switch->output(ValueSwitch::kSwitch);
  if (audio_rate)
    return control_rate_total;

  if (details.value_scale == ValueDetails::kQuadratic) {
    Processor* scale = nullptr;
    if (details.post_offset)
      scale = new cr::Quadratic(details.post_offset);
    else
      scale = new cr::Square();

    scale->plug(control_rate_total);
    addProcessor(scale);
    control_rate_total = scale->output();
  } else if (details.value_scale == ValueDetails::kCubic) {
    Processor* scale = nullptr;
    VITAL_ASSERT(details.post_offset == 0.0f);
    if (details.post_offset)
      scale = new cr::Cubic(details.post_offset);
    else
      scale = new cr::Cube();

    scale->plug(control_rate_total);
    addProcessor(scale);
    control_rate_total = scale->output();
  } else if (details.value_scale == ValueDetails::kQuartic) {
    Processor* scale = nullptr;
    VITAL_ASSERT(details.post_offset == 0.0f);
    if (details.post_offset)
      scale = new cr::Quartic(details.post_offset);
    else
      scale = new cr::Quart();

    scale->plug(control_rate_total);
    addProcessor(scale);
    control_rate_total = scale->output();
  } else if (details.value_scale == ValueDetails::kExponential) {
    Processor* exponential = new cr::ExponentialScale(details.min, details.max, 2.0f, details.post_offset);
    exponential->plug(control_rate_total);
    addProcessor(exponential);
    control_rate_total = exponential->output();
  } else if (details.value_scale == ValueDetails::kSquareRoot) {
    cr::Root* root = new cr::Root(details.post_offset);
    root->plug(control_rate_total);
    addProcessor(root);
    control_rate_total = root->output();
  }

  return control_rate_total;
}

Output* SynthModule::createTempoSyncSwitch(std::string name, Processor* frequency, const Output* beats_per_second, bool poly, Input* midi) {
  Output* tempo = nullptr;
  if (poly)
    tempo = createPolyModControl(name + "_tempo");
  else
    tempo = createMonoModControl(name + "_tempo");

  cr::Value* sync = new cr::Value(1.0f);
  data_->controls[name + "_sync"] = sync;
  control_map_["sync"] = sync;
  addIdleProcessor(sync);

  TempoChooser* tempo_chooser = new TempoChooser();
  tempo_chooser->plug(sync, TempoChooser::kSync);
  tempo_chooser->plug(tempo, TempoChooser::kTempoIndex);
  tempo_chooser->plug(frequency, TempoChooser::kFrequency);
  tempo_chooser->plug(beats_per_second, TempoChooser::kBeatsPerSecond);

  if (midi) {
    Output* keytrack_transpose = nullptr;
    Output* keytrack_tune = nullptr;
    if (poly) {
      keytrack_transpose = createPolyModControl(name + "_keytrack_transpose");
      keytrack_tune = createPolyModControl(name + "_keytrack_tune");
    } else {
      keytrack_transpose = createMonoModControl(name + "_keytrack_transpose");
      keytrack_tune = createMonoModControl(name + "_keytrack_tune");
    }

    tempo_chooser->plug(keytrack_transpose, TempoChooser::kKeytrackTranspose);
    tempo_chooser->plug(keytrack_tune, TempoChooser::kKeytrackTune);
    tempo_chooser->useInput(midi, TempoChooser::kMidi);
  }

  if (poly)
    addProcessor(tempo_chooser);
  else
    addMonoProcessor(tempo_chooser);

  return tempo_chooser->output();
}

void SynthModule::createStatusOutput(std::string name, Output* source) {
  data_->status_outputs[name] = std::make_unique<StatusOutput>(source);
}

control_map SynthModule::getControls() {
  control_map all_controls = data_->controls;
  for (SynthModule* sub_module : data_->sub_modules) {
    control_map sub_controls = sub_module->getControls();
    all_controls.insert(sub_controls.begin(), sub_controls.end());
  }

  return all_controls;
}

Output* SynthModule::getModulationSource(std::string name) {
  if (data_->mod_sources.count(name))
    return data_->mod_sources[name];

  for (SynthModule* sub_module : data_->sub_modules) {
    Output* source = sub_module->getModulationSource(name);
    if (source)
      return source;
  }

  return nullptr;
}

const StatusOutput* SynthModule::getStatusOutput(std::string name) const {
  if (data_->status_outputs.count(name))
    return data_->status_outputs.at(name).get();

  for (SynthModule* sub_module : data_->sub_modules) {
    const StatusOutput* source = sub_module->getStatusOutput(name);
    if (source)
      return source;
  }

  return nullptr;
}

Processor* SynthModule::getModulationDestination(std::string name, bool poly) {
  Processor* poly_destination = getPolyModulationDestination(name);

  if (poly && poly_destination)
    return poly_destination;

  return getMonoModulationDestination(name);
}

Processor* SynthModule::getMonoModulationDestination(std::string name) {
  if (data_->mono_mod_destinations.count(name))
    return data_->mono_mod_destinations[name];

  for (SynthModule* sub_module : data_->sub_modules) {
    Processor* destination = sub_module->getMonoModulationDestination(name);
    if (destination)
      return destination;
  }

  return nullptr;
}

Processor* SynthModule::getPolyModulationDestination(std::string name) {
  if (data_->poly_mod_destinations.count(name)) {
    return data_->poly_mod_destinations[name];
  }

  for (SynthModule* sub_module : data_->sub_modules) {
    Processor* destination = sub_module->getPolyModulationDestination(name);
    if (destination)
      return destination;
  }

  return nullptr;
}

ValueSwitch* SynthModule::getModulationSwitch(std::string name, bool poly) {
  if (poly)
    return getPolyModulationSwitch(name);
  return getMonoModulationSwitch(name);
}

ValueSwitch* SynthModule::getMonoModulationSwitch(std::string name) {
  if (data_->mono_modulation_switches.count(name))
    return data_->mono_modulation_switches[name];

  for (SynthModule* sub_module : data_->sub_modules) {
    ValueSwitch* value_switch = sub_module->getMonoModulationSwitch(name);
    if (value_switch)
      return value_switch;
  }

  return nullptr;
}

ValueSwitch* SynthModule::getPolyModulationSwitch(std::string name) {
  if (data_->poly_modulation_switches.count(name))
    return data_->poly_modulation_switches[name];

  for (SynthModule* sub_module : data_->sub_modules) {
    ValueSwitch* value_switch = sub_module->getPolyModulationSwitch(name);
    if (value_switch)
      return value_switch;
  }

  return nullptr;
}

void SynthModule::updateAllModulationSwitches() {
  for (auto& mod_switch : data_->mono_modulation_switches) {
    bool enable = data_->mono_mod_destinations[mod_switch.first]->connectedInputs() > 1;
    if (data_->poly_mod_destinations.count(mod_switch.first))
      enable = enable || data_->poly_mod_destinations[mod_switch.first]->connectedInputs() > 0;
    mod_switch.second->set(enable ? 1.0f : 0.0f);
  }

  for (auto& mod_switch : data_->poly_modulation_switches)
    mod_switch.second->set(data_->poly_mod_destinations[mod_switch.first]->connectedInputs() > 0);

  for (SynthModule* sub_module : data_->sub_modules)
    sub_module->updateAllModulationSwitches();
}

output_map& SynthModule::getModulationSources() {
  output_map& all_sources = data_->mod_sources;
  for (SynthModule* sub_module : data_->sub_modules) {
    output_map& sub_sources = sub_module->getModulationSources();
    all_sources.insert(sub_sources.begin(), sub_sources.end());
  }

  return all_sources;
}

input_map& SynthModule::getMonoModulationDestinations() {
  input_map& all_destinations = data_->mono_mod_destinations;
  for (SynthModule* sub_module : data_->sub_modules) {
    input_map& sub_destinations = sub_module->getMonoModulationDestinations();
    all_destinations.insert(sub_destinations.begin(), sub_destinations.end());
  }

  return all_destinations;
}

input_map& SynthModule::getPolyModulationDestinations() {
  input_map& all_destinations = data_->poly_mod_destinations;
  for (SynthModule* sub_module : data_->sub_modules) {
    input_map& sub_destinations = sub_module->getPolyModulationDestinations();
    all_destinations.insert(sub_destinations.begin(), sub_destinations.end());
  }

  return all_destinations;
}

output_map& SynthModule::getMonoModulations() {
  output_map& all_readouts = data_->mono_modulation_readout;
  for (SynthModule* sub_module : data_->sub_modules) {
    output_map& sub_readouts = sub_module->getMonoModulations();
    all_readouts.insert(sub_readouts.begin(), sub_readouts.end());
  }

  return all_readouts;
}

output_map& SynthModule::getPolyModulations() {
  output_map& all_readouts = data_->poly_modulation_readout;
  for (SynthModule* sub_module : data_->sub_modules) {
    output_map& sub_readouts = sub_module->getPolyModulations();
    all_readouts.insert(sub_readouts.begin(), sub_readouts.end());
  }

  return all_readouts;
}

void SynthModule::enableOwnedProcessors(bool enable) {
  for (Processor* processor : data_->owned_mono_processors)
    processor->enable(enable);
  for (SynthModule* sub_module : data_->sub_modules)
    sub_module->enable(enable);
}

void SynthModule::enable(bool enable) {
  if (enabled() == enable)
    return;

  ProcessorRouter::enable(enable);
  enableOwnedProcessors(enable);
}

void SynthModule::addMonoProcessor(Processor* processor, bool own) {
  getMonoRouter()->addProcessor(processor);
  if (own)
    data_->owned_mono_processors.push_back(processor);
}

void SynthModule::addIdleMonoProcessor(Processor* processor) {
  getMonoRouter()->addIdleProcessor(processor);
}

Value* SynthModule::createBaseControl2(AddControlInput input) {
  Value* val = nullptr;
  if (input.audio_rate) {
    if (input.smooth_value) {
      val = new SmoothValue(input.default_value);
      addMonoProcessor(val, false);
    } else {
      val = new Value(input.default_value);
      addIdleMonoProcessor(val);
    }
  } else {
    if (input.smooth_value) {
      val = new cr::SmoothValue(input.default_value);
      addMonoProcessor(val, false);
    } else {
      val = new cr::Value(input.default_value);
      addIdleMonoProcessor(val);
    }
  }

  control_map_[input.name] = val;
  return val;
}

Output* SynthModule::createPolyModControl2(AddControlInput input) {
  auto details = input;
  Output* base_control = createBaseModControl2(input);

  Processor* poly_total;
  if (input.audio_rate) {
    poly_total = new ModulationSum();
    if (input.reset) {
      poly_total->useInput(input.reset, ModulationSum::kReset);
    }
  } else {
    poly_total = new cr::VariableAdd();
  }

  addProcessor(poly_total);
  data_->poly_mod_destinations[input.name] = poly_total;

  Processor* modulation_total;
  if (input.audio_rate)
    modulation_total = new Add();
  else
    modulation_total = new cr::Add();

  modulation_total->plug(base_control, 0);
  modulation_total->plug(poly_total, 1);
  addProcessor(modulation_total);

  data_->poly_modulation_readout[input.name] = poly_total->output();

  ValueSwitch* control_switch = new ValueSwitch(0.0f);
  control_switch->plugNext(base_control);
  control_switch->plugNext(modulation_total);

  if (input.internal_modulation) {
    poly_total->plugNext(input.internal_modulation);
    control_switch->set(1);
  } else {
    control_switch->addProcessor(poly_total);
    control_switch->addProcessor(modulation_total);
    control_switch->set(0);
  }
  addIdleProcessor(control_switch);
  data_->poly_modulation_switches[input.name] = control_switch;

  Output* control_rate_total = control_switch->output(ValueSwitch::kSwitch);
  if (input.audio_rate)
    return control_rate_total;

  if (details.value_scale == ValueDetails::kQuadratic) {
    Processor* scale = nullptr;
    if (details.post_offset)
      scale = new cr::Quadratic(details.post_offset);
    else
      scale = new cr::Square();

    scale->plug(control_rate_total);
    addProcessor(scale);
    control_rate_total = scale->output();
  } else if (details.value_scale == ValueDetails::kCubic) {
    Processor* scale = nullptr;
    VITAL_ASSERT(details.post_offset == 0.0f);
    if (details.post_offset)
      scale = new cr::Cubic(details.post_offset);
    else
      scale = new cr::Cube();

    scale->plug(control_rate_total);
    addProcessor(scale);
    control_rate_total = scale->output();
  } else if (details.value_scale == ValueDetails::kQuartic) {
    Processor* scale = nullptr;
    VITAL_ASSERT(details.post_offset == 0.0f);
    if (details.post_offset)
      scale = new cr::Quartic(details.post_offset);
    else
      scale = new cr::Quart();

    scale->plug(control_rate_total);
    addProcessor(scale);
    control_rate_total = scale->output();
  } else if (details.value_scale == ValueDetails::kExponential) {
    Processor* exponential = new cr::ExponentialScale(details.min, details.max, 2.0f, details.post_offset);
    exponential->plug(control_rate_total);
    addProcessor(exponential);
    control_rate_total = exponential->output();
  } else if (details.value_scale == ValueDetails::kSquareRoot) {
    cr::Root* root = new cr::Root(details.post_offset);
    root->plug(control_rate_total);
    addProcessor(root);
    control_rate_total = root->output();
  }

  return control_rate_total;
}

Output* SynthModule::createBaseModControl2(AddControlInput input) {
  Processor* base_val = createBaseControl2(input);

  Processor* mono_total = nullptr;
  if (input.audio_rate)
    mono_total = new ModulationSum();
  else
    mono_total = new cr::VariableAdd();

  // std::cout << "mono_total: " << mono_total << " base_val: " << base_val << std::endl;

  mono_total->plugNext(base_val);
  addMonoProcessor(mono_total, false);
  if (input.name == "amount") std::cout << " amount mono_total: " << mono_total << std::endl;
  data_->mono_mod_destinations[input.name] = mono_total;
  data_->mono_modulation_readout[input.name] = mono_total->output();

  ValueSwitch* control_switch = new ValueSwitch(0.0f);
  control_switch->plugNext(base_val);
  control_switch->plugNext(mono_total);

  if (input.internal_modulation)
    mono_total->plugNext(input.internal_modulation);
  else
    control_switch->addProcessor(mono_total);

  addIdleMonoProcessor(control_switch);
  if (input.smooth_value || input.internal_modulation)
    control_switch->set(1);
  else
    control_switch->set(0);
  data_->mono_modulation_switches[input.name] = control_switch;

  return control_switch->output(ValueSwitch::kSwitch);
}

// Output* SynthModule::createMonoModControl2(AddControlInput input) {
//   Output* control_rate_total = createBaseModControl2(input);
//   if (audio_rate)
//     return control_rate_total;

//   if (input.value_scale == ValueDetails::kQuadratic) {
//     Processor* scale = nullptr;
//     if (input.post_offset)
//       scale = new cr::Quadratic(input.post_offset);
//     else
//       scale = new cr::Square();

//     scale->plug(control_rate_total);
//     addMonoProcessor(scale);
//     control_rate_total = scale->output();
//   } else if (input.value_scale == ValueDetails::kCubic) {
//     Processor* scale = nullptr;
//     VITAL_ASSERT(input.post_offset == 0.0f);
//     if (input.post_offset)
//       scale = new cr::Cubic(input.post_offset);
//     else
//       scale = new cr::Cube();

//     scale->plug(control_rate_total);
//     addMonoProcessor(scale);
//     control_rate_total = scale->output();
//   } else if (input.value_scale == ValueDetails::kQuartic) {
//     Processor* scale = nullptr;
//     VITAL_ASSERT(input.post_offset == 0.0f);
//     if (input.post_offset)
//       scale = new cr::Quartic(input.post_offset);
//     else
//       scale = new cr::Quart();

//     scale->plug(control_rate_total);
//     addMonoProcessor(scale);
//     control_rate_total = scale->output();
//   } else if (input.value_scale == ValueDetails::kExponential) {
//     cr::ExponentialScale* exponential = new cr::ExponentialScale(input.min, input.max, 2.0f);
//     exponential->plug(control_rate_total);
//     addMonoProcessor(exponential);
//     control_rate_total = exponential->output();
//   } else if (input.value_scale == ValueDetails::kSquareRoot) {
//     cr::Root* root = new cr::Root(input.post_offset);
//     root->plug(control_rate_total);
//     addMonoProcessor(root);
//     control_rate_total = root->output();
//   }

//   return control_rate_total;
// }

Output* SynthModule::createMonoModControl2(AddControlInput input) {
  Output* control_rate_total = createBaseModControl2(input);
  if (input.audio_rate)
    return control_rate_total;

  if (input.value_scale == ValueDetails::kQuadratic) {
    Processor* scale = nullptr;
    if (input.post_offset)
      scale = new cr::Quadratic(input.post_offset);
    else
      scale = new cr::Square();

    scale->plug(control_rate_total);
    addMonoProcessor(scale);
    control_rate_total = scale->output();
  } else if (input.value_scale == ValueDetails::kCubic) {
    Processor* scale = nullptr;
    VITAL_ASSERT(input.post_offset == 0.0f);
    if (input.post_offset)
      scale = new cr::Cubic(input.post_offset);
    else
      scale = new cr::Cube();

    scale->plug(control_rate_total);
    addMonoProcessor(scale);
    control_rate_total = scale->output();
  } else if (input.value_scale == ValueDetails::kQuartic) {
    Processor* scale = nullptr;
    VITAL_ASSERT(input.post_offset == 0.0f);
    if (input.post_offset)
      scale = new cr::Quartic(input.post_offset);
    else
      scale = new cr::Quart();

    scale->plug(control_rate_total);
    addMonoProcessor(scale);
    control_rate_total = scale->output();
  } else if (input.value_scale == ValueDetails::kExponential) {
    cr::ExponentialScale* exponential = new cr::ExponentialScale(input.min, input.max, 2.0f);
    exponential->plug(control_rate_total);
    addMonoProcessor(exponential);
    control_rate_total = exponential->output();
  } else if (input.value_scale == ValueDetails::kSquareRoot) {
    cr::Root* root = new cr::Root(input.post_offset);
    root->plug(control_rate_total);
    addMonoProcessor(root);
    control_rate_total = root->output();
  }

  return control_rate_total;
}

Output* SynthModule::createTempoSyncSwitch2(AddControlInput input, Processor* frequency, const Output* beats_per_second, bool poly, Input* midi, std::string sync_name) {
  auto name = input.name;
  Output* tempo = nullptr;
  if (poly)
    tempo = createPolyModControl2(input);
  else
    tempo = createMonoModControl2(input);

  cr::Value* sync = new cr::Value(1.0f);
  data_->controls[name + "_" + sync_name] = sync;
  control_map_[sync_name] = sync;
  addIdleProcessor(sync);

  TempoChooser* tempo_chooser = new TempoChooser();
  tempo_chooser->plug(sync, TempoChooser::kSync);
  tempo_chooser->plug(tempo, TempoChooser::kTempoIndex);
  tempo_chooser->plug(frequency, TempoChooser::kFrequency);
  tempo_chooser->plug(beats_per_second, TempoChooser::kBeatsPerSecond);

  if (midi) {
    Output* keytrack_transpose = nullptr;
    Output* keytrack_tune = nullptr;
    AddControlInput keytrack_transpose_input = { .name = "tempo_keytrack_transpose", .value_scale = ValueDetails::kIndexed , .min = -60.0, .max = 36.0, .default_value = -12.0 };
    AddControlInput keytrack_tune_input = { .name = "tempo_keytrack_tune", .min = -1.0, .max = 1.0 };  
    if (poly) {
      keytrack_transpose = createPolyModControl2(keytrack_transpose_input);
      keytrack_tune = createPolyModControl2(keytrack_tune_input);
    } else {
      keytrack_transpose = createMonoModControl2(keytrack_transpose_input);
      keytrack_tune = createMonoModControl2(keytrack_tune_input);
    }

    tempo_chooser->plug(keytrack_transpose, TempoChooser::kKeytrackTranspose);
    tempo_chooser->plug(keytrack_tune, TempoChooser::kKeytrackTune);
    tempo_chooser->useInput(midi, TempoChooser::kMidi);
  }

  if (poly)
    addProcessor(tempo_chooser);
  else
    addMonoProcessor(tempo_chooser);

  return tempo_chooser->output();
}

} // namespace vital

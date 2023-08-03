// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use cras::pseudonymization::Salt;
use serde::de::Visitor;
use serde::Deserialize;
use serde::Serialize;

#[derive(Serialize, Deserialize)]
struct LocalState {
    settings: Settings,
}

#[derive(Serialize, Deserialize)]
struct Settings {
    audio: Audio,
}

#[derive(Serialize, Deserialize)]
struct Audio {
    device_state: Option<HashMap<AudioNode, DeviceState>>,
    devices: Option<Devices>,
    input_user_priority: Option<HashMap<AudioNode, i32>>,
    output_user_priority: Option<HashMap<AudioNode, i32>>,
    // The last_seen field is intentionally dropped to avoid
    // joining feedback reports. See b/279545748#comment11.
}

#[derive(PartialEq, Eq, Hash, Debug)]
struct AudioNode {
    stable_id: u32,
    is_input: bool,
}

enum AudioNodeParseError {
    MissingDelimiter,
    StableIdNotU32,
    InvalidIsInput,
}

impl AudioNode {
    fn try_from_str(v: &str) -> Result<Self, AudioNodeParseError> {
        let (stable_id_str, is_input_str) = v
            .split_once(" : ")
            .ok_or_else(|| AudioNodeParseError::MissingDelimiter)?;
        let stable_id = stable_id_str
            .parse::<u32>()
            .map_err(|_| AudioNodeParseError::StableIdNotU32)?;
        let is_input = match is_input_str {
            "0" => false,
            "1" => true,
            _ => return Err(AudioNodeParseError::InvalidIsInput),
        };
        Ok(AudioNode {
            stable_id,
            is_input,
        })
    }
}

impl Serialize for AudioNode {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        serializer.serialize_str(&format!(
            "0x{:x} : {}",
            Salt::instance().pseudonymize_stable_id(self.stable_id),
            self.is_input as i32
        ))
    }
}

struct AudioNodeVisitor;

impl<'de> Visitor<'de> for AudioNodeVisitor {
    type Value = AudioNode;

    fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
        formatter.write_str("a audio node string")
    }

    fn visit_str<E>(self, v: &str) -> Result<Self::Value, E>
    where
        E: serde::de::Error,
    {
        use AudioNodeParseError::*;
        match AudioNode::try_from_str(v) {
            Ok(node) => Ok(node),
            Err(err) => Err(match err {
                MissingDelimiter => E::custom("missing delimiter"),
                StableIdNotU32 => E::custom("stable id is not u32"),
                InvalidIsInput => E::custom("invalid is_input"),
            }),
        }
    }
}

impl<'de> Deserialize<'de> for AudioNode {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        deserializer.deserialize_string(AudioNodeVisitor)
    }
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
struct DeviceState {
    activate_by_user: bool,
    active: bool,
}

#[derive(Serialize, Deserialize)]
struct Devices {
    gain_percent: Option<HashMap<AudioNode, i32>>,
    mute: Option<HashMap<AudioNode, i32>>,
    volume_percent: Option<HashMap<AudioNode, i32>>,
}

#[cfg(test)]
mod tests {
    use super::AudioNode;
    use super::DeviceState;
    use super::LocalState;

    use cras::pseudonymization::Salt;

    #[test]
    fn audio_node() {
        let a = AudioNode {
            stable_id: 0,
            is_input: true,
        };
        let a_json = "\"0 : 1\"";
        let a_json_salted = format!("\"0x{:x} : 1\"", Salt::instance().pseudonymize_stable_id(0));
        let b = AudioNode {
            stable_id: 56789,
            is_input: false,
        };
        let b_json = "\"56789 : 0\"";
        let b_json_salted = format!(
            "\"0x{:x} : 0\"",
            Salt::instance().pseudonymize_stable_id(56789)
        );
        assert_eq!(serde_json::to_string(&a).unwrap(), a_json_salted);
        assert_eq!(serde_json::to_string(&b).unwrap(), b_json_salted);
        assert_eq!(serde_json::from_str::<AudioNode>(&a_json).unwrap(), a);
        assert_eq!(serde_json::from_str::<AudioNode>(&b_json).unwrap(), b);
    }

    #[test]
    fn deserialize() {
        let audio_settings_str = r#"{
  "device_state": {
    "1923447123 : 0": {
      "activate_by_user": false,
      "active": true
    },
    "3787040 : 1": {
      "activate_by_user": false,
      "active": true
    }
  },
  "devices": {
    "gain_percent": {
      "2315562897 : 1": 50,
      "2356475750 : 1": 50,
      "3787040 : 1": 50,
      "3962083865 : 1": 50
    },
    "mute": {
      "1923447123 : 0": 0
    },
    "volume_percent": {
      "1923447123 : 0": 75
    }
  },
  "input_user_priority": {
    "3787040 : 1": 1
  },
  "last_seen": {
    "1923447123 : 0": 1687158819.326577,
    "2315562897 : 1": 1687158819.326577,
    "2356475750 : 1": 1687158819.326577,
    "3787040 : 1": 1687158819.326577,
    "3962083865 : 1": 1687158819.326577
  },
  "output_user_priority": {
    "1923447123 : 0": 1
  }
}"#;
        let local_state_string = r#"{"settings":{"audio":@AUDIO_SETTINGS@}}"#
            .replace("@AUDIO_SETTINGS@", audio_settings_str);
        let local_state: LocalState = serde_json::from_str(local_state_string.as_str()).unwrap();
        let audio = local_state.settings.audio;
        assert_eq!(
            *audio
                .device_state
                .as_ref()
                .unwrap()
                .get(&AudioNode {
                    stable_id: 1923447123,
                    is_input: false,
                })
                .as_ref()
                .unwrap(),
            &DeviceState {
                activate_by_user: false,
                active: true
            }
        );
        assert_eq!(
            *audio
                .devices
                .as_ref()
                .unwrap()
                .gain_percent
                .as_ref()
                .unwrap()
                .get(&AudioNode {
                    stable_id: 2315562897,
                    is_input: true,
                })
                .unwrap(),
            50
        );
        assert_eq!(
            *audio
                .devices
                .as_ref()
                .unwrap()
                .mute
                .as_ref()
                .unwrap()
                .get(&AudioNode {
                    stable_id: 1923447123,
                    is_input: false
                })
                .unwrap(),
            0
        );
        assert_eq!(
            *audio
                .devices
                .as_ref()
                .unwrap()
                .volume_percent
                .as_ref()
                .unwrap()
                .get(&AudioNode {
                    stable_id: 1923447123,
                    is_input: false
                })
                .unwrap(),
            75
        );
        assert_eq!(
            *audio
                .input_user_priority
                .as_ref()
                .unwrap()
                .get(&AudioNode {
                    stable_id: 3787040,
                    is_input: true
                })
                .unwrap(),
            1
        );
        assert_eq!(
            *audio
                .output_user_priority
                .as_ref()
                .unwrap()
                .get(&AudioNode {
                    stable_id: 1923447123,
                    is_input: false
                })
                .unwrap(),
            1
        )
    }
}

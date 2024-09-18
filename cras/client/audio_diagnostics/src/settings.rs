// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::BTreeSet;
use std::collections::HashMap;

use anyhow::Context;
use cras_common::pseudonymization::Salt;
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
    input_preference_set: Option<HashMap<AudioNodeSet, AudioNode>>,
    output_preference_set: Option<HashMap<AudioNodeSet, AudioNode>>,
    most_recent_activated_input_device_ids: Option<Vec<AudioNode>>,
    most_recent_activated_output_device_ids: Option<Vec<AudioNode>>,
    // The last_seen field is intentionally dropped to avoid
    // joining feedback reports. See b/279545748#comment11.
}

#[derive(PartialEq, Eq, Hash, Debug, Ord, PartialOrd)]
struct AudioNode {
    stable_id: u32,
    is_input: bool,
}

enum AudioNodeParseError {
    MissingDelimiter,
    StableIdNotU32,
    InvalidIsInput,
}

impl AudioNodeParseError {
    fn as_str(&self) -> &'static str {
        match self {
            AudioNodeParseError::MissingDelimiter => "missing delimiter",
            AudioNodeParseError::StableIdNotU32 => "stable id is not u32",
            AudioNodeParseError::InvalidIsInput => "invalid is_input",
        }
    }
}

impl AudioNode {
    fn try_from_str(v: &str) -> Result<Self, AudioNodeParseError> {
        let (stable_id_str, is_input_str) = v
            .split_once(" : ")
            .ok_or(AudioNodeParseError::MissingDelimiter)?;
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

fn format_audio_node(node: &AudioNode) -> String {
    format!(
        "0x{:x} : {}",
        Salt::instance().pseudonymize_stable_id(node.stable_id),
        node.is_input as i32
    )
}

impl Serialize for AudioNode {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        serializer.serialize_str(&format_audio_node(self))
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
        match AudioNode::try_from_str(v) {
            Ok(node) => Ok(node),
            Err(err) => Err(E::custom(err.as_str())),
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

#[derive(PartialEq, Eq, Hash, Debug)]
struct AudioNodeSet {
    audio_nodes: BTreeSet<AudioNode>,
}

impl<'de> Deserialize<'de> for AudioNodeSet {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let mut audio_nodes = BTreeSet::new();
        let audio_nodes_string = String::deserialize(deserializer)?;
        for part in audio_nodes_string.split(',') {
            let audio_node = AudioNode::try_from_str(part.trim())
                .map_err(|err| serde::de::Error::custom(err.as_str()))?;
            audio_nodes.insert(audio_node);
        }
        Ok(AudioNodeSet { audio_nodes })
    }
}

impl Serialize for AudioNodeSet {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let mut audio_nodes_string = String::new();
        for (i, node) in self.audio_nodes.iter().enumerate() {
            if i > 0 {
                audio_nodes_string.push_str(", ");
            }
            audio_nodes_string.push_str(&format_audio_node(node));
        }
        serializer.serialize_str(&audio_nodes_string)
    }
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
struct DeviceState {
    activate_by_user: Option<bool>,
    active: bool,
}

#[derive(Serialize, Deserialize)]
struct Devices {
    gain_percent: Option<HashMap<AudioNode, f64>>,
    mute: Option<HashMap<AudioNode, i32>>,
    volume_percent: Option<HashMap<AudioNode, f64>>,
}

fn get_salted_audio_settings(path: &str) -> Result<String, anyhow::Error> {
    let contents =
        std::fs::read_to_string(path).with_context(|| "error reading Local State file")?;
    let local_state: LocalState =
        serde_json::from_str(contents.as_str()).with_context(|| "error parsing local state")?;
    serde_json::to_string_pretty(&local_state).with_context(|| "error serializing local state")
}

pub fn print_salted_audio_settings(path: &str) {
    println!("=== audio settings ===");
    match get_salted_audio_settings(path) {
        Ok(settings) => println!("{settings}"),
        Err(err) => println!("{err:#}"),
    };
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeSet;

    use cras_common::pseudonymization::Salt;

    use super::AudioNode;
    use super::AudioNodeSet;
    use super::DeviceState;
    use super::LocalState;

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
        assert_eq!(serde_json::from_str::<AudioNode>(a_json).unwrap(), a);
        assert_eq!(serde_json::from_str::<AudioNode>(b_json).unwrap(), b);
    }

    #[test]
    fn audio_node_set() {
        let stable_id1 = 1;
        let stable_id2 = 2;
        let input_set = AudioNodeSet {
            audio_nodes: BTreeSet::from([
                AudioNode {
                    stable_id: stable_id1,
                    is_input: true,
                },
                AudioNode {
                    stable_id: stable_id2,
                    is_input: true,
                },
            ]),
        };
        let input_set_json = format!("\"{:x} : 1, {:x} : 1\"", stable_id1, stable_id2);
        let input_set_json_salted = format!(
            "\"0x{:x} : 1, 0x{:x} : 1\"",
            Salt::instance().pseudonymize_stable_id(stable_id1),
            Salt::instance().pseudonymize_stable_id(stable_id2)
        );
        assert_eq!(
            serde_json::to_string(&input_set).unwrap(),
            input_set_json_salted
        );
        assert_eq!(
            serde_json::from_str::<AudioNodeSet>(&input_set_json).unwrap(),
            input_set
        );

        let stable_id3 = 3;
        let stable_id4 = 4;
        let output_set = AudioNodeSet {
            audio_nodes: BTreeSet::from([
                AudioNode {
                    stable_id: stable_id3,
                    is_input: false,
                },
                AudioNode {
                    stable_id: stable_id4,
                    is_input: false,
                },
            ]),
        };
        let output_set_json = format!("\"{:x} : 0, {:x} : 0\"", stable_id3, stable_id4);
        let output_set_json_salted = format!(
            "\"0x{:x} : 0, 0x{:x} : 0\"",
            Salt::instance().pseudonymize_stable_id(stable_id3),
            Salt::instance().pseudonymize_stable_id(stable_id4)
        );
        assert_eq!(
            serde_json::to_string(&output_set).unwrap(),
            output_set_json_salted
        );
        assert_eq!(
            serde_json::from_str::<AudioNodeSet>(&output_set_json).unwrap(),
            output_set
        );
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
      "active": true
    }
  },
  "devices": {
    "gain_percent": {
      "2315562897 : 1": 50.0,
      "2356475750 : 1": 50.0,
      "3787040 : 1": 50.0,
      "3962083865 : 1": 50.0
    },
    "mute": {
      "1923447123 : 0": 0
    },
    "volume_percent": {
      "1923447123 : 0": 75.0
    }
  },
  "input_user_priority": {
    "3787040 : 1": 1
  },
  "input_preference_set": {
    "3787040 : 1, 2315562897 : 1, 3962083865 : 1": "2315562897 : 1"
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
  },
  "output_preference_set": {
    "2356475750 : 0, 1923447123 : 0, 2315562897 : 0": "1923447123 : 0"
  },
  "most_recent_activated_input_device_ids": [
    "3787040 : 1", "2315562897 : 1", "3962083865 : 1"
  ],
  "most_recent_activated_output_device_ids": [
    "2356475750 : 0", "1923447123 : 0", "2315562897 : 0"
  ]
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
                activate_by_user: Some(false),
                active: true
            }
        );
        assert_eq!(
            *audio
                .device_state
                .as_ref()
                .unwrap()
                .get(&AudioNode {
                    stable_id: 3787040,
                    is_input: true,
                })
                .as_ref()
                .unwrap(),
            &DeviceState {
                activate_by_user: None,
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
            50.
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
            75.
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
                .input_preference_set
                .as_ref()
                .unwrap()
                .get(&AudioNodeSet {
                    audio_nodes: BTreeSet::from([
                        AudioNode {
                            stable_id: 3962083865,
                            is_input: true,
                        },
                        AudioNode {
                            stable_id: 3787040,
                            is_input: true,
                        },
                        AudioNode {
                            stable_id: 2315562897,
                            is_input: true,
                        },
                    ])
                })
                .unwrap(),
            AudioNode {
                stable_id: 2315562897,
                is_input: true
            }
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
        );
        assert_eq!(
            *audio
                .output_preference_set
                .as_ref()
                .unwrap()
                .get(&AudioNodeSet {
                    audio_nodes: BTreeSet::from([
                        AudioNode {
                            stable_id: 2315562897,
                            is_input: false,
                        },
                        AudioNode {
                            stable_id: 2356475750,
                            is_input: false,
                        },
                        AudioNode {
                            stable_id: 1923447123,
                            is_input: false,
                        },
                    ])
                })
                .unwrap(),
            AudioNode {
                stable_id: 1923447123,
                is_input: false
            }
        );
        assert_eq!(
            *audio
                .most_recent_activated_input_device_ids
                .as_ref()
                .unwrap(),
            vec![
                AudioNode {
                    stable_id: 3787040,
                    is_input: true,
                },
                AudioNode {
                    stable_id: 2315562897,
                    is_input: true,
                },
                AudioNode {
                    stable_id: 3962083865,
                    is_input: true,
                },
            ]
        );
        assert_eq!(
            *audio
                .most_recent_activated_output_device_ids
                .as_ref()
                .unwrap(),
            vec![
                AudioNode {
                    stable_id: 2356475750,
                    is_input: false,
                },
                AudioNode {
                    stable_id: 1923447123,
                    is_input: false,
                },
                AudioNode {
                    stable_id: 2315562897,
                    is_input: false,
                },
            ]
        );
    }
}

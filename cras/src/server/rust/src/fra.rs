// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::collections::HashMap;

/// cbindgen:enum-class
#[repr(C)]
#[derive(Debug, Copy, Clone)]
// The log is monitoring by FRA. The corresponding definition is on
// `google3/chromeos/feedback/analyzer/signals`,
// The order and values of existing CrasFRASignal variants should not be changed.
// Please add new variants from the end.
pub enum CrasFRASignal {
    PeripheralsUsbSoundCard = 0,
    USBAudioConfigureFailed,
    USBAudioListOutputNodeFailed,
    USBAudioStartFailed,
    USBAudioSoftwareVolumeAbnormalRange,
    USBAudioSoftwareVolumeAbnormalSteps,
    USBAudioUCMNoJack,
    USBAudioUCMWrongJack,
    USBAudioResumeFailed,
    ActiveOutputDevice,
    ActiveInputDevice,
    AudioThreadEvent,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct KeyValuePair {
    pub key: *const ::libc::c_char,
    pub value: *const ::libc::c_char,
}

pub struct FRALog {
    pub signal: CrasFRASignal,
    pub context: HashMap<String, String>,
}

impl std::fmt::Display for FRALog {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "AudioFRA:{} context:{}",
            self.signal as u32,
            serde_json::to_string(&self.context).unwrap()
        )
    }
}

/// Macro to simplify the creation and printing of `FRALog` instances.
///
/// # Syntax
///
///
/// fra!(signal, context)
///
///
/// # Example
///
/// ```
/// use cras::fra;
/// use cras::fra::{FRALog, CrasFRASignal};
/// use std::collections::HashMap;
///
/// fn main() {
/// fra!(CrasFRASignal::PeripheralsUsbSoundCard, HashMap::from([(String::from("key1"), String::from("value1"))]))
///
/// }
/// ```
///
#[macro_export]
macro_rules! fra {
    ($signal:expr, $context:expr) => {{
        let fra = FRALog {
            signal: $signal,
            context: $context,
        };
        log::info!("{}", fra);
    }};
}

pub mod bindings {
    pub use super::CrasFRASignal;
    use super::FRALog;
    pub use super::KeyValuePair;
    use crate::fra;

    /// This function is called from C code to log a FRA event.
    ///
    /// # Arguments
    ///
    /// * `signal` - The type of FRA event to log.
    /// * `num` - The number of context pairs.
    /// * `context_arr` - A pointer to an array of `KeyValuePair` structs.
    ///
    /// # Safety
    /// The memory pointed by context_arr must contains valid array of `KeyValuePair` structs.
    /// The memory pointed by KeyValuePair::key and KeyValuePair::value must contains a valid nul terminator at the end of the string.
    #[no_mangle]
    pub unsafe extern "C" fn fralog(
        signal: CrasFRASignal,
        num: usize,
        context_arr: *const KeyValuePair,
    ) {
        let mut context = std::collections::HashMap::new();
        unsafe {
            let slice = std::slice::from_raw_parts(context_arr, num);

            for pair in slice {
                let key = std::ffi::CStr::from_ptr(pair.key)
                    .to_string_lossy()
                    .into_owned();
                let value = std::ffi::CStr::from_ptr(pair.value)
                    .to_string_lossy()
                    .into_owned();
                context.insert(key, value);
            }
        }
        fra!(signal, context);
    }
}

mod tests {

    #[test]
    fn test_order() {
        use crate::fra::CrasFRASignal;

        assert_eq!(CrasFRASignal::PeripheralsUsbSoundCard as u32, 0);
        assert_eq!(CrasFRASignal::USBAudioConfigureFailed as u32, 1);
        assert_eq!(CrasFRASignal::USBAudioListOutputNodeFailed as u32, 2);
        assert_eq!(CrasFRASignal::USBAudioStartFailed as u32, 3);
        assert_eq!(CrasFRASignal::USBAudioSoftwareVolumeAbnormalRange as u32, 4);
        assert_eq!(CrasFRASignal::USBAudioSoftwareVolumeAbnormalSteps as u32, 5);
        assert_eq!(CrasFRASignal::USBAudioUCMNoJack as u32, 6);
        assert_eq!(CrasFRASignal::USBAudioUCMWrongJack as u32, 7);
        assert_eq!(CrasFRASignal::USBAudioResumeFailed as u32, 8);
        assert_eq!(CrasFRASignal::ActiveOutputDevice as u32, 9);
        assert_eq!(CrasFRASignal::ActiveInputDevice as u32, 10);
    }
}

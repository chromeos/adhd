// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod bindings;
mod utf8cstring;

use std::path::Path;

use anyhow::anyhow;
use anyhow::Context;
use indexmap::IndexMap;
use utf8cstring::Utf8CString;

pub type CrasIniMap = IndexMap<Utf8CString, IndexMap<Utf8CString, Utf8CString>>;

fn parse_string(s: impl Into<String>) -> anyhow::Result<CrasIniMap> {
    let mut dict = IndexMap::<Utf8CString, IndexMap<Utf8CString, Utf8CString>>::new();

    let ini = configparser::ini::Ini::new()
        .read(s.into())
        .map_err(|err| anyhow!("configparser load failed {err}"))?;
    for (section, items) in ini {
        let section_utf8c =
            Utf8CString::try_from(section.clone()).context("invalid section name {section:?}")?;

        let mut kvmap = IndexMap::new();
        for (k, v) in items {
            let k_utf8c = Utf8CString::try_from(k.clone())
                .context("section {section:?} key {k:?} is invalid")?;
            let v = v.with_context(|| {
                format!("section {section:?} key {k:?} without value is invalid")
            })?;
            let v_utf8c = Utf8CString::try_from(v)
                .with_context(|| format!("section {section:?} key {k:?} has invalid value"))?;
            assert!(kvmap.insert(k_utf8c, v_utf8c).is_none());
        }
        assert!(dict.insert(section_utf8c, kvmap).is_none());
    }

    Ok(dict)
}

/// Parse the ini file stored at `path`.
pub fn parse_file(path: &Path) -> anyhow::Result<CrasIniMap> {
    let s = std::fs::read_to_string(path)?;
    parse_string(s)
}

#[cfg(test)]
mod tests {
    use crate::parse_string;

    fn check_error_message<T>(result: anyhow::Result<T>, substring: &str) {
        assert!(
            result.is_err_and(|err| {
                let msg = format!("{err:#}");
                assert!(
                    msg.contains(substring),
                    "{msg:?} does not contain {substring:?}"
                );
                true
            }),
            "result is not an Err(_)"
        );
    }

    #[test]
    fn invalid_syntax() {
        check_error_message(parse_string("[["), "configparser load failed");
    }

    #[test]
    fn invalid_section_name() {
        check_error_message(parse_string("[aa\0]"), "invalid section name");
    }

    #[test]
    fn invalid_missing_value() {
        check_error_message(
            parse_string("[aa]\nx"),
            r#"section "aa" key "x" without value is invalid"#,
        );
    }

    #[test]
    fn invalid_value() {
        check_error_message(
            parse_string("[aa]\nx=\0"),
            r#"section "aa" key "x" has invalid value"#,
        );
    }

    #[test]
    fn simple() {
        let d = parse_string(
            r#"[aa]
x=5
[aa] # duplicate section
y=6
[bb]
x=7 # hash comment
y=8; semicolon comment
"#,
        )
        .unwrap();
        assert_eq!(d.get_index(0).unwrap().0.as_str(), "aa");
        assert_eq!(d.get_index(1).unwrap().0.as_str(), "bb");
        assert_eq!(
            d.get("aa").expect("key").get("x").expect("value").as_str(),
            "5"
        );
        assert_eq!(d.get("aa").unwrap().get("y").unwrap().as_str(), "6");
        assert!(d.get("aa").unwrap().get("z").is_none());
        assert_eq!(d.get("bb").unwrap().get("x").unwrap().as_str(), "7");
        assert_eq!(d.get("bb").unwrap().get("y").unwrap().as_str(), "8");
        assert!(d.get("bb").unwrap().get("z").is_none());
    }
}

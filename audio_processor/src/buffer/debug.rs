// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Debug;
use std::iter::Iterator;

use crate::MultiBuffer;
use crate::MultiSlice;

fn debug_format<T>(struct_: &str, element: &str, lengths: T) -> String
where
    T: Iterator<Item = usize>,
{
    format!(
        "{} {{ <{} of lengths [{}]> }}",
        struct_,
        element,
        lengths
            .map(|x| x.to_string())
            .collect::<Vec<_>>()
            .join(", ")
    )
}

impl<T> Debug for MultiBuffer<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "MultiBuffer {{ shape: {:?} }}", self.shape)
    }
}

impl<T> Debug for MultiSlice<'_, T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let debug = debug_format("MultiSlice", "slices", self.data.iter().map(|x| x.len()));
        f.write_str(&debug)
    }
}

#[cfg(test)]
mod tests {
    use crate::MultiBuffer;
    use crate::MultiSlice;

    #[test]
    fn multi_buffer() {
        let buf = MultiBuffer::from(vec![vec![1i32, 2], vec![3, 4], vec![5, 6]]);
        assert_eq!(
            format!("{:?}", buf),
            "MultiBuffer { shape: Shape { channels: 3, frames: 2 } }"
        );
    }

    #[test]
    fn multi_slice() {
        let mut buf = vec![vec![1i32], vec![2, 3], vec![4, 5, 6]];
        assert_eq!(
            format!("{:?}", MultiSlice::from_vecs(&mut buf)),
            "MultiSlice { <slices of lengths [1, 2, 3]> }"
        );
    }
}

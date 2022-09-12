// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Trait to support slice casting.
pub trait SliceCast<'a> {
    /// Cast the slice.
    /// This is a "view" cast. The underlying memory is unchanged.
    /// Panics if the length or alignment is invalid for `T`.
    fn cast<T: Sized>(self) -> &'a mut [T];
}

impl<'a, T> SliceCast<'a> for &'a mut [T]
where
    T: Sized,
{
    fn cast<U: Sized>(self) -> &'a mut [U] {
        let bytes = std::mem::size_of::<T>() * self.len();
        if bytes % std::mem::size_of::<U>() != 0 {
            panic!(
                "Cannot convert {} x {} to {}",
                self.len(),
                std::any::type_name::<T>(),
                std::any::type_name::<U>(),
            );
        }

        unsafe {
            if self.as_ptr() as usize % std::mem::align_of::<U>() != 0 {
                panic!(
                    "Pointer 0x{:x} is misaligned for {}",
                    self.as_ptr() as usize,
                    std::any::type_name::<U>(),
                )
            }

            std::slice::from_raw_parts_mut(
                self.as_mut_ptr().cast::<U>(),
                bytes / std::mem::size_of::<U>(),
            )
        }
    }
}

#[cfg(test)]
mod tests {
    use super::SliceCast;

    #[test]
    fn byte_to_u32() {
        let mut arr = [1u8, 2, 3, 4, 5, 6, 7, 8];

        let u32slice: &mut [u32] = (&mut arr[..]).cast();
        assert_eq!(
            u32slice,
            [
                u32::from_ne_bytes([1u8, 2, 3, 4]),
                u32::from_ne_bytes([5u8, 6, 7, 8]),
            ]
        );

        u32slice[0] = 0x12345678;
        u32slice[1] = 0x23456789;

        assert_eq!(
            &arr[..],
            0x12345678u32
                .to_ne_bytes()
                .into_iter()
                .chain(0x23456789u32.to_ne_bytes())
                .collect::<Vec<u8>>()
        );
    }

    #[test]
    fn u32_to_byte() {
        let mut arr = [0x12345678u32, 0x23456789u32];

        let u8slice: &mut [u8] = (&mut arr[..]).cast();
        assert_eq!(
            u8slice,
            0x12345678u32
                .to_ne_bytes()
                .into_iter()
                .chain(0x23456789u32.to_ne_bytes())
                .collect::<Vec<u8>>()
        );

        for (i, x) in u8slice.iter_mut().enumerate() {
            *x = i as u8 + 1;
        }
        assert_eq!(
            &arr[..],
            [
                u32::from_ne_bytes([1u8, 2, 3, 4]),
                u32::from_ne_bytes([5u8, 6, 7, 8]),
            ]
        );
    }

    #[test]
    #[should_panic(expected = "Cannot convert 7 x u8 to f32")]
    fn panic_incompatible_size() {
        let slice = unsafe { std::slice::from_raw_parts_mut(4 as *mut u8, 7) };
        let _f32slice: &mut [f32] = slice.cast();
    }

    #[test]
    #[should_panic(expected = "Pointer 0x32 is misaligned for f32")]
    fn panic_incompatible_alignment() {
        let slice = unsafe { std::slice::from_raw_parts_mut(50 as *mut u8, 8) };
        let _f32slice: &mut [f32] = slice.cast();
    }
}

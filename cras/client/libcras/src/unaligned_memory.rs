// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::marker::PhantomData;

use data_model::DataInit;

/// A memory location that supports unaligned access of a `T`.
#[derive(Debug)]
pub struct UnalignedRef<'a, T: DataInit>
where
    T: 'a,
{
    addr: *mut T,
    phantom: PhantomData<&'a T>,
}

impl<'a, T: DataInit> UnalignedRef<'a, T> {
    /// Creates a reference to raw unaligned memory that must support unaligned access of `T`
    /// sized chunks.
    ///
    /// # Safety
    /// To use this safely, the caller must guarantee that the memory at `addr` is big enough for a
    /// `T` and is available for the duration of the lifetime of the new `UnalignedRef`. The caller
    /// must also guarantee that all other users of the given chunk of memory are using unaligned
    /// accesses.
    ///
    /// # Caveat
    /// Caller should make sure the lifetime of [`UnalignedRef`] and `addr` are matched.
    pub unsafe fn new(addr: *mut T) -> UnalignedRef<'a, T> {
        UnalignedRef {
            addr,
            phantom: PhantomData,
        }
    }

    /// Does a unaligned write of the value `v` to the address of this ref.
    #[inline(always)]
    pub fn store(&self, v: T) {
        // Safety: Assume we have a valid unaligned address with type T
        unsafe { self.addr.write_unaligned(v) };
    }

    /// Does a unaligned read of the value at the address of this ref.
    #[inline(always)]
    pub fn load(&self) -> T {
        // Safety: Assume we have a valid unaligned address with type T
        unsafe { self.addr.read_unaligned() }
    }
}

#[cfg(test)]
mod tests {
    use std::ptr;

    use super::*;

    #[repr(packed)]
    #[derive(Copy, Clone, Debug, PartialEq)]
    struct Packed {
        f1: u8,
        f2: u16,
    }

    #[test]
    fn unaligned_memory_test() {
        let mut p = Packed { f1: 1, f2: 2 };
        let unaligned_ref = unsafe { UnalignedRef::new(ptr::addr_of_mut!(p.f2)) };
        assert_eq!(unaligned_ref.load(), 2);
        unaligned_ref.store(500);
        assert_eq!(p, Packed { f1: 1, f2: 500 });
    }
}

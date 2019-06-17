// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::io;
use std::mem;
use std::os::unix::io::{AsRawFd, RawFd};
use std::ptr;
use std::ptr::NonNull;
use std::slice;

use libc;

use cras_sys::gen::{
    cras_audio_shm_header, cras_server_state, CRAS_NUM_SHM_BUFFERS, CRAS_SHM_BUFFERS_MASK,
};
use data_model::VolatileRef;

/// A structure wrapping a fd which contains a shared `cras_audio_shm_header`.
/// * `shm_fd` - A shared memory fd contains a `cras_audio_shm_header`
pub struct CrasAudioShmHeaderFd {
    fd: CrasShmFd,
}

impl CrasAudioShmHeaderFd {
    /// Creates a `CrasAudioShmHeaderFd` by shared memory fd
    /// # Arguments
    /// * `fd` - A shared memory file descriptor, which will be owned by the resulting structure and
    /// the fd will be closed on drop.
    ///
    /// # Returns
    /// A structure wrapping a `CrasShmFd` with the input fd and `size` which equals to
    /// the size of `cras_audio_shm_header`.
    ///
    /// To use this function safely, we need to make sure
    /// - The input fd is a valid shared memory fd.
    /// - The input shared memory fd won't be used by others.
    /// - The shared memory area in the input fd contains a `cras_audio_shm_header`.
    pub unsafe fn new(fd: libc::c_int) -> Self {
        Self {
            fd: CrasShmFd::new(fd, mem::size_of::<cras_audio_shm_header>()),
        }
    }
}

/// A wrapper for the raw structure `cras_audio_shm_header` with
/// size information for the separate audio samples shm area and several
/// `VolatileRef` to sub fields for safe access to the header.
#[allow(dead_code)]
pub struct CrasAudioHeader<'a> {
    addr: *mut libc::c_void,
    /// Size of the buffer for samples in CrasAudioBuffer
    samples_len: usize,
    used_size: VolatileRef<'a, u32>,
    frame_size: VolatileRef<'a, u32>,
    read_buf_idx: VolatileRef<'a, u32>,
    write_buf_idx: VolatileRef<'a, u32>,
    read_offset: [VolatileRef<'a, u32>; CRAS_NUM_SHM_BUFFERS as usize],
    write_offset: [VolatileRef<'a, u32>; CRAS_NUM_SHM_BUFFERS as usize],
}

// It is safe to send audio buffers between threads as this struct has exclusive ownership of the
// pointers contained in it.
unsafe impl<'a> Send for CrasAudioHeader<'a> {}

/// An unsafe macro for getting `VolatileRef` for a field from a given NonNull pointer.
/// It Supports
/// - Nested sub-field
/// - Element of an array field
///
/// To use this macro safely, we need to
/// - Make sure the pointer address is readable and writable for its structure.
/// - Make sure all `VolatileRef`s generated from this macro have exclusive ownership for the same
/// pointer.
#[macro_export]
macro_rules! vref_from_addr {
    ($addr:ident, $($field:ident).*) => {
        VolatileRef::new(&mut $addr.as_mut().$($field).* as *mut _)
    };

    ($addr:ident, $field:ident[$idx:tt]) => {
        VolatileRef::new(&mut $addr.as_mut().$field[$idx] as *mut _)
    };
}

// Generates error when an index is out of range.
fn index_out_of_range() -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, "Index out of range.")
}

impl<'a> CrasAudioHeader<'a> {
    // Creates a `CrasAudioHeader` with given `CrasAudioShmHeaderFd` and `samples_len`
    fn new(header_fd: CrasAudioShmHeaderFd, samples_len: usize) -> io::Result<Self> {
        // Safe because the creator of CrasAudioShmHeaderFd already
        // ensured that header_fd contains a cras_audio_shm_header.
        let mmap_addr = unsafe {
            cras_mmap(
                header_fd.fd.size,
                libc::PROT_READ | libc::PROT_WRITE,
                header_fd.fd.as_raw_fd(),
            )?
        };

        let mut addr = NonNull::new(mmap_addr as *mut cras_audio_shm_header)
            .ok_or_else(|| io::Error::new(io::ErrorKind::Other, "Failed to create header."))?;

        // Safe because we know that mmap_addr (contained in addr) contains a
        // cras_audio_shm_header, and the mapped area will be exclusively
        // owned by this struct.
        unsafe {
            Ok(CrasAudioHeader {
                addr: addr.as_ptr() as *mut libc::c_void,
                samples_len,
                used_size: vref_from_addr!(addr, config.used_size),
                frame_size: vref_from_addr!(addr, config.frame_bytes),
                read_buf_idx: vref_from_addr!(addr, read_buf_idx),
                write_buf_idx: vref_from_addr!(addr, write_buf_idx),
                read_offset: [
                    vref_from_addr!(addr, read_offset[0]),
                    vref_from_addr!(addr, read_offset[1]),
                ],
                write_offset: [
                    vref_from_addr!(addr, write_offset[0]),
                    vref_from_addr!(addr, write_offset[1]),
                ],
            })
        }
    }

    /// Gets the write offset of the buffer and the writable length.
    ///
    /// # Returns
    ///
    ///  * (`usize`, `usize`) - write offset in bytes and buffer length in bytes.
    pub fn get_offset_and_len(&self) -> (usize, usize) {
        let used_size = self.get_used_size();
        let offset = self.get_write_buf_idx() as usize * used_size;
        (offset, used_size)
    }

    /// Gets the read offset of the readable buffer.
    ///
    ///  # Returns
    ///
    ///  * `usize` - read offset in bytes
    pub fn get_read_offset(&self) -> usize {
        self.get_read_buf_idx() as usize * self.get_used_size()
    }

    /// Gets the number of bytes per frame from the shared memory structure.
    ///
    /// # Returns
    ///
    /// * `usize` - Number of bytes per frame
    pub fn get_frame_size(&self) -> usize {
        self.frame_size.load() as usize
    }

    /// Gets the size in bytes of the shared memory buffer.
    fn get_used_size(&self) -> usize {
        self.used_size.load() as usize
    }

    /// Gets the index of the current written buffer.
    ///
    /// # Returns
    /// `u32` - the returned index is less than `CRAS_NUM_SHM_BUFFERS`.
    fn get_write_buf_idx(&self) -> u32 {
        self.write_buf_idx.load() & CRAS_SHM_BUFFERS_MASK
    }

    fn get_read_buf_idx(&self) -> u32 {
        self.read_buf_idx.load() & CRAS_SHM_BUFFERS_MASK
    }

    /// Switches the written buffer.
    fn switch_write_buf_idx(&mut self) {
        self.write_buf_idx
            .store(self.get_write_buf_idx() as u32 ^ 1u32)
    }

    /// Switches the buffer to read.
    fn switch_read_buf_idx(&mut self) {
        self.read_buf_idx
            .store(self.get_read_buf_idx() as u32 ^ 1u32)
    }

    /// Checks if the offset value for setting write_offset or read_offset is
    /// out of range or not.
    ///
    /// # Arguments
    /// `offset` - 0 <= `offset` <= `used_size` && `offset` + `used_size` <=
    /// `samples_len`. Writable or readable size equals to 0 when offset equals
    /// to `used_size`.
    ///
    /// # Errors
    /// Returns an error if `offset` is out of range.
    fn check_offset(&self, offset: u32) -> io::Result<()> {
        if offset as usize <= self.get_used_size()
            && offset as usize + self.get_used_size() <= self.samples_len
        {
            Ok(())
        } else {
            Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "Offset out of range.",
            ))
        }
    }

    /// Sets `write_offset[idx]` to the count of written bytes.
    ///
    /// # Arguments
    /// `idx` - 0 <= `idx` < `CRAS_NUM_SHM_BUFFERS`
    /// `offset` - 0 <= `offset` <= `used_size` && `offset` + `used_size` <=
    /// `samples_len`. Writable size equals to 0 when offset equals to
    /// `used_size`.
    ///
    /// # Errors
    /// Returns an error if `offset` is out of range.
    fn set_write_offset(&mut self, idx: usize, offset: u32) -> io::Result<()> {
        self.check_offset(offset)?;
        let write_offset = self.write_offset.get(idx).ok_or_else(index_out_of_range)?;
        write_offset.store(offset);
        Ok(())
    }

    /// Sets `read_offset[idx]` of to count of written bytes.
    ///
    /// # Arguments
    /// `idx` - 0 <= `idx` < `CRAS_NUM_SHM_BUFFERS`
    /// `offset` - 0 <= `offset` <= `used_size` && `offset` + `used_size` <=
    /// `samples_len`. Readable size equals to 0 when offset equals to
    /// `used_size`.
    ///
    /// # Errors
    /// Returns error if index out of range.
    fn set_read_offset(&mut self, idx: usize, offset: u32) -> io::Result<()> {
        self.check_offset(offset)?;
        let read_offset = self.read_offset.get(idx).ok_or_else(index_out_of_range)?;
        read_offset.store(offset);
        Ok(())
    }

    /// Commits written frames by switching the current buffer to the other one
    /// after samples are ready and indexes of current buffer are all set.
    /// - Sets `write_offset` of current buffer to `frame_count * frame_size`
    /// - Sets `read_offset` of current buffer to `0`.
    ///
    /// # Arguments
    ///
    /// * `frame_count` - Number of frames written to the current buffer
    ///
    /// # Errors
    ///
    /// * Returns error if `frame_count` is larger than buffer size
    ///
    /// This function is safe because we switch `write_buf_idx` after letting
    /// `write_offset` and `read_offset` ready and we read / write shared memory
    /// variables with volatile operations.
    pub fn commit_written_frames(&mut self, frame_count: u32) -> io::Result<()> {
        // Uses `u64` to prevent possible overflow
        let byte_count = frame_count as u64 * self.get_frame_size() as u64;
        if byte_count > self.get_used_size() as u64 {
            Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "frame_count * frame_size is larger than used_size",
            ))
        } else {
            let idx = self.get_write_buf_idx() as usize;
            // Sets `write_offset` of current buffer to frame_count * frame_size
            self.set_write_offset(idx, byte_count as u32)?;
            // Sets `read_offset` of current buffer to `0`.
            self.set_read_offset(idx, 0)?;
            // Switch to the other buffer
            self.switch_write_buf_idx();
            Ok(())
        }
    }

    /// Get readable frames in current buffer.
    ///
    /// # Returns
    ///
    /// * `usize` - number of readable frames.
    ///
    /// # Errors
    ///
    /// Returns error if index out of range.
    pub fn get_readable_frames(&self) -> io::Result<usize> {
        let idx = self.get_read_buf_idx() as usize;
        let read_offset = self.read_offset.get(idx).ok_or_else(index_out_of_range)?;
        let write_offset = self.write_offset.get(idx).ok_or_else(index_out_of_range)?;
        let nframes =
            (write_offset.load() as i32 - read_offset.load() as i32) / self.get_frame_size() as i32;
        if nframes < 0 {
            Ok(0)
        } else {
            Ok(nframes as usize)
        }
    }

    /// Commit read frames from reader, .
    /// - Sets `read_offset` of current buffer to `read_offset + frame_count * frame_size`.
    /// If `read_offset` is larger than or equal to `write_offset`, then
    /// - Sets `read_offset` and `write_offset` to `0` and switch `read_buf_idx`.
    ///
    /// # Arguments
    ///
    /// * `frame_count` - Read frames in current read buffer.
    ///
    /// # Errors
    ///
    /// Returns error if index out of range.
    pub fn commit_read_frames(&mut self, frame_count: u32) -> io::Result<()> {
        let idx = self.get_read_buf_idx() as usize;
        let read_offset = self.read_offset.get(idx).ok_or_else(index_out_of_range)?;
        let write_offset = self.write_offset.get(idx).ok_or_else(index_out_of_range)?;
        read_offset.store(read_offset.load() + frame_count * self.get_frame_size() as u32);
        if read_offset.load() >= write_offset.load() {
            read_offset.store(0);
            write_offset.store(0);
            self.switch_read_buf_idx();
        }
        Ok(())
    }
}

impl<'a> Drop for CrasAudioHeader<'a> {
    fn drop(&mut self) {
        // Safe because all references must be gone by the time drop is called.
        unsafe {
            libc::munmap(self.addr as *mut _, mem::size_of::<cras_audio_shm_header>());
        }
    }
}

// To use this safely, we need to make sure
// - The given fd contains valid space which is larger than `len` + `offset`
unsafe fn cras_mmap_offset(
    len: usize,
    prot: libc::c_int,
    fd: libc::c_int,
    offset: usize,
) -> io::Result<*mut libc::c_void> {
    if offset > libc::off_t::max_value() as usize {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "Requested offset is out of range of `libc::off_t`.",
        ));
    }
    // It's safe because we handle its returned results.
    match libc::mmap(
        ptr::null_mut(),
        len,
        prot,
        libc::MAP_SHARED,
        fd,
        offset as libc::off_t,
    ) {
        libc::MAP_FAILED => Err(io::Error::last_os_error()),
        shm_ptr => Ok(shm_ptr),
    }
}

// To use this safely, we need to make sure
// - The given fd contains valid space which is larger than `len`
unsafe fn cras_mmap(
    len: usize,
    prot: libc::c_int,
    fd: libc::c_int,
) -> io::Result<*mut libc::c_void> {
    cras_mmap_offset(len, prot, fd, 0)
}

/// A structure that points to RO shared memory area - `cras_server_state`
/// The structure is created from a shared memory fd which contains the structure.
#[allow(dead_code)]
pub struct CrasServerState {
    addr: *mut libc::c_void,
    size: usize,
}

impl CrasServerState {
    /// An unsafe function for creating `CrasServerState`. To use this function safely, we need to
    /// - Make sure that the `shm_fd` must come from the server's message that provides the shared
    /// memory region. The Id for the message is `CRAS_CLIENT_MESSAGE_ID::CRAS_CLIENT_CONNECTED`.
    #[allow(dead_code)]
    pub unsafe fn new(shm_fd: CrasShmFd) -> io::Result<Self> {
        let size = mem::size_of::<cras_server_state>();
        if size > shm_fd.size {
            Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "Invalid shared memory size.",
            ))
        } else {
            let addr = cras_mmap(size, libc::PROT_READ, shm_fd.as_raw_fd())?;
            Ok(CrasServerState { addr, size })
        }
    }

    // Gets `cras_server_state` reference from the structure.
    #[allow(dead_code)]
    fn get_ref(&self) -> VolatileRef<cras_server_state> {
        unsafe { VolatileRef::new(self.addr as *mut _) }
    }
}

impl Drop for CrasServerState {
    /// Call `munmap` for `addr`.
    fn drop(&mut self) {
        unsafe {
            // Safe because all references must be gone by the time drop is called.
            libc::munmap(self.addr, self.size);
        }
    }
}

/// A structure holding the mapped shared memory area used to exchange
/// samples with CRAS. The shared memory is owned exclusively by this structure,
/// and will be cleaned up on drop.
/// * `addr` - The address of the mapped shared memory.
/// * `len` - Length of the mapped shared memory in bytes.
pub struct CrasAudioBuffer {
    addr: *mut u8,
    len: usize,
}

// It is safe to send audio buffers between threads as this struct has exclusive ownership of the
// shared memory area contained in it.
unsafe impl Send for CrasAudioBuffer {}

impl CrasAudioBuffer {
    fn new(samples_fd: CrasShmFd) -> io::Result<Self> {
        // This is safe because we checked that the size of the shm in samples_fd
        // was at least samples_fd.size when it was created.
        let addr = unsafe {
            cras_mmap(
                samples_fd.size,
                libc::PROT_READ | libc::PROT_WRITE,
                samples_fd.as_raw_fd(),
            )? as *mut u8
        };
        Ok(Self {
            addr,
            len: samples_fd.size,
        })
    }

    /// Provides a mutable slice to be filled with audio samples.
    pub fn get_buffer(&mut self) -> &mut [u8] {
        // This is safe because it takes a mutable reference to self, and there can only be one
        // taken at a time. Although this is shared memory, the reader side must have it mapped as
        // read only.
        unsafe { slice::from_raw_parts_mut(self.addr, self.len) }
    }
}

impl Drop for CrasAudioBuffer {
    fn drop(&mut self) {
        // Safe because all references must be gone by the time drop is called.
        unsafe {
            libc::munmap(self.addr as *mut _, self.len);
        }
    }
}

/// Creates header and buffer from given shared memory fds.
pub fn create_header_and_buffers<'a>(
    header_fd: CrasAudioShmHeaderFd,
    samples_fd: CrasShmFd,
) -> io::Result<(CrasAudioHeader<'a>, CrasAudioBuffer)> {
    let header = CrasAudioHeader::new(header_fd, samples_fd.size)?;
    let buffer = CrasAudioBuffer::new(samples_fd)?;

    Ok((header, buffer))
}

/// A structure wrapping a fd which contains a shared memory area and its size.
/// * `fd` - The shared memory file descriptor, a `libc::c_int`.
/// * `size` - Size of the shared memory area.
pub struct CrasShmFd {
    fd: libc::c_int,
    size: usize,
}

impl CrasShmFd {
    /// Creates a `CrasShmFd` by shared memory fd and size
    /// # Arguments
    /// * `fd` - A shared memory file descriptor, which will be owned by the resulting structure and
    /// the fd will be closed on drop.
    /// * `size` - Size of the shared memory.
    ///
    /// # Returns
    /// * `CrasShmFd` - Wrap the input arguments without doing anything.
    ///
    /// To use this function safely, we need to make sure
    /// - The input fd is a valid shared memory fd.
    /// - The input shared memory fd won't be used by others.
    /// - The input fd contains memory size larger than `size`.
    pub unsafe fn new(fd: libc::c_int, size: usize) -> CrasShmFd {
        CrasShmFd { fd, size }
    }
}

impl AsRawFd for CrasShmFd {
    fn as_raw_fd(&self) -> RawFd {
        self.fd
    }
}

impl Drop for CrasShmFd {
    fn drop(&mut self) {
        // It's safe here if we make sure
        // - the input fd is valid and
        // - `CrasShmFd` is the only owner
        // in `new` function
        unsafe {
            libc::close(self.fd);
        }
    }
}

/// A structure wrapping a fd which contains a shared `cras_server_state`.
/// * `shm_fd` - A shared memory fd contains a `cras_server_state`
pub struct CrasServerStateShmFd {
    #[allow(dead_code)]
    shm_fd: CrasShmFd,
}

impl CrasServerStateShmFd {
    /// Creates a `CrasServerStateShmFd` by shared memory fd
    /// # Arguments
    /// * `fd` - A shared memory file descriptor, which will be owned by the resulting structure and
    /// the fd will be closed on drop.
    ///
    /// # Returns
    /// A structure wrapping a `CrasShmFd` with the input fd and `size` which equals to
    /// the size of `cras_server_sate`.
    ///
    /// To use this function safely, we need to make sure
    /// - The input fd is a valid shared memory fd.
    /// - The input shared memory fd won't be used by others.
    /// - The shared memory area in the input fd contains a `cras_server_state`.
    pub unsafe fn new(fd: libc::c_int) -> Self {
        Self {
            shm_fd: CrasShmFd::new(fd, mem::size_of::<cras_server_state>()),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;
    #[test]
    fn cras_audio_header_switch_test() {
        let mut header = create_cras_audio_header("/tmp_cras_audio_header1", 0);
        assert_eq!(0, header.get_write_buf_idx());
        header.switch_write_buf_idx();
        assert_eq!(1, header.get_write_buf_idx());
    }

    #[test]
    fn cras_audio_header_write_offset_test() {
        let mut header = create_cras_audio_header("/tmp_cras_audio_header2", 20);
        header.frame_size.store(2);
        header.used_size.store(5);

        assert_eq!(0, header.write_offset[0].load());
        // Index out of bound
        assert!(header.set_write_offset(2, 5).is_err());
        // Offset out of bound
        assert!(header.set_write_offset(0, 6).is_err());
        assert_eq!(0, header.write_offset[0].load());
        assert!(header.set_write_offset(0, 5).is_ok());
        assert_eq!(5, header.write_offset[0].load());
        mem::forget(header);
    }

    #[test]
    fn cras_audio_header_read_offset_test() {
        let mut header = create_cras_audio_header("/tmp_cras_audio_header3", 20);
        header.frame_size.store(2);
        header.used_size.store(5);

        assert_eq!(0, { header.read_offset[0].load() });
        // Index out of bound
        assert!(header.set_read_offset(2, 5).is_err());
        // Offset out of bound
        assert!(header.set_read_offset(0, 6).is_err());
        assert_eq!(0, header.read_offset[0].load());
        assert!(header.set_read_offset(0, 5).is_ok());
        assert_eq!(5, header.read_offset[0].load());
    }

    #[test]
    fn cras_audio_header_commit_written_frame_test() {
        let mut header = create_cras_audio_header("/tmp_cras_audio_header4", 20);
        header.frame_size.store(2);
        header.used_size.store(10);
        header.read_offset[0].store(10);
        assert!(header.commit_written_frames(5).is_ok());
        assert_eq!(header.write_offset[0].load(), 10);
        assert_eq!(header.read_offset[0].load(), 0);
        assert_eq!(header.write_buf_idx.load(), 1);
    }

    #[test]
    fn cras_audio_header_get_readable_frames_test() {
        let header = create_cras_audio_header("/tmp_cras_audio_header5", 20);
        header.frame_size.store(2);
        header.used_size.store(10);
        header.read_offset[0].store(2);
        header.write_offset[0].store(10);
        let frames = header
            .get_readable_frames()
            .expect("Failed to get readable frames.");
        assert_eq!(frames, 4);
    }

    #[test]
    fn cras_audio_header_commit_read_frames_test() {
        let mut header = create_cras_audio_header("/tmp_cras_audio_header6", 20);
        header.frame_size.store(2);
        header.used_size.store(10);
        header.read_offset[0].store(2);
        header.write_offset[0].store(10);
        header
            .commit_read_frames(3)
            .expect("Failed to commit read frames.");
        assert_eq!(header.get_read_buf_idx(), 0);
        assert_eq!(header.read_offset[0].load(), 8);

        header
            .commit_read_frames(1)
            .expect("Failed to commit read frames.");
        // Read buffer should be switched
        assert_eq!(header.get_read_buf_idx(), 1);
        assert_eq!(header.read_offset[0].load(), 0);
        assert_eq!(header.read_offset[0].load(), 0);
    }

    #[test]
    fn create_header_and_buffers_test() {
        let header_fd = cras_audio_header_fd("/tmp_audio_shm_header");
        let samples_fd = cras_audio_samples_fd("/tmp_audio_shm_samples", 20);
        let res = create_header_and_buffers(header_fd, samples_fd);
        res.expect("Failed to create header and buffer.");
    }

    fn create_cras_audio_header(name: &str, samples_len: usize) -> CrasAudioHeader {
        CrasAudioHeader::new(cras_audio_header_fd(name), samples_len).unwrap()
    }

    fn cras_audio_header_fd(name: &str) -> CrasAudioShmHeaderFd {
        let size = mem::size_of::<cras_audio_shm_header>();
        let fd = cras_shm_open_rw(name, size);
        unsafe { CrasAudioShmHeaderFd::new(fd) }
    }

    fn cras_audio_samples_fd(name: &str, len: usize) -> CrasShmFd {
        let fd = cras_shm_open_rw(name, len);
        unsafe { CrasShmFd::new(fd, len) }
    }

    fn cras_shm_open_rw(name: &str, size: usize) -> libc::c_int {
        unsafe {
            let cstr_name = CString::new(name).expect("cras_shm_open_rw: new CString failed");
            let fd = libc::shm_open(
                cstr_name.as_ptr() as *const _,
                libc::O_CREAT | libc::O_EXCL | libc::O_RDWR,
                0x0600,
            );
            assert_ne!(fd, -1, "cras_shm_open_rw: shm_open error");
            libc::ftruncate(fd, size as libc::off_t);
            fd
        }
    }

    #[test]
    fn cras_mmap_pass() {
        let fd = cras_shm_open_rw("/tmp_cras_shm_test_1", 100);
        let rc = unsafe { cras_mmap(10, libc::PROT_READ, fd) };
        assert!(rc.is_ok());
        unsafe { libc::munmap(rc.unwrap(), 10) };
    }

    #[test]
    fn cras_mmap_failed() {
        let rc = unsafe { cras_mmap(10, libc::PROT_READ, -1) };
        assert!(rc.is_err());
    }
}

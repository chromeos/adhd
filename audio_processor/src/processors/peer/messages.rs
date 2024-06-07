// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::IoSlice;
use std::io::IoSliceMut;
use std::os::fd::AsRawFd;
use std::os::fd::BorrowedFd;
use std::os::fd::OwnedFd;
use std::slice;

use anyhow::bail;
use anyhow::ensure;
use anyhow::Context;
use nix::sys::socket::recvmsg;
use nix::sys::socket::sendmsg;
use nix::sys::socket::socketpair;
use nix::sys::socket::AddressFamily;
use nix::sys::socket::MsgFlags;
use nix::sys::socket::SockFlag;
use nix::sys::socket::SockType;
use serde::Deserialize;
use serde::Serialize;

use crate::config;
use crate::Format;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Debug)]
pub(super) enum RequestOp {
    Init,
    Process,
    Stop,
}

pub(super) const INIT_MAX_SIZE: usize = 4096;

#[derive(Serialize, Deserialize)]
pub(super) struct Init {
    pub input_format: Format,
    pub config: config::Processor,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) enum ResponseOp {
    Ok,
    Error,
}

pub(super) trait Op: Sized {
    fn as_u8(&self) -> u8;
    fn try_from_u8(op: u8) -> anyhow::Result<Self>;
}

impl Op for RequestOp {
    fn as_u8(&self) -> u8 {
        *self as u8
    }

    fn try_from_u8(op: u8) -> anyhow::Result<Self> {
        if op == RequestOp::Init as u8 {
            Ok(RequestOp::Init)
        } else if op == RequestOp::Process as u8 {
            Ok(RequestOp::Process)
        } else if op == RequestOp::Stop as u8 {
            Ok(RequestOp::Stop)
        } else {
            bail!("unexpected Op {op}");
        }
    }
}

impl Op for ResponseOp {
    fn as_u8(&self) -> u8 {
        *self as u8
    }

    fn try_from_u8(op: u8) -> anyhow::Result<Self> {
        if op == ResponseOp::Ok as u8 {
            Ok(ResponseOp::Ok)
        } else if op == ResponseOp::Error as u8 {
            Ok(ResponseOp::Error)
        } else {
            bail!("unexpected Op {op}");
        }
    }
}

pub(super) fn send<'a>(
    fd: BorrowedFd,
    op: impl Op,
    iov: impl Iterator<Item = IoSlice<'a>>,
) -> anyhow::Result<()> {
    let op_byte = op.as_u8();
    let iov_with_op = std::iter::once(IoSlice::new(slice::from_ref(&op_byte)))
        .chain(
            iov
                // Make the borrow checker happy.
                .map(|x| x),
        )
        .collect::<Vec<_>>();
    sendmsg::<()>(fd.as_raw_fd(), &iov_with_op, &[], MsgFlags::empty(), None).context("sendmsg")?;
    Ok(())
}

pub(super) fn send_str(fd: BorrowedFd, op: impl Op, s: &str) -> anyhow::Result<()> {
    send(fd, op, std::iter::once(IoSlice::new(s.as_bytes())))
}

pub(super) fn recv<'a, T: Op>(fd: BorrowedFd, buf: &mut [u8]) -> anyhow::Result<(T, usize)> {
    let mut op_byte = 255u8;

    let mut iov = [
        IoSliceMut::new(slice::from_mut(&mut op_byte)),
        IoSliceMut::new(buf),
    ];
    let r = recvmsg::<()>(fd.as_raw_fd(), &mut iov, None, MsgFlags::empty()).context("recvmsg")?;
    let payload_len = r.bytes.checked_sub(1).context("empty message")?;
    ensure!(!r.flags.contains(MsgFlags::MSG_TRUNC), "message too long");
    Ok((T::try_from_u8(op_byte)?, payload_len))
}

pub(super) fn recv_slice<'a, T: Op>(
    fd: BorrowedFd,
    buf: &'a mut [u8],
) -> anyhow::Result<(T, &'a mut [u8])> {
    let (op, len) = recv(fd, buf)?;
    Ok((op, &mut buf[..len]))
}

pub fn create_socketpair() -> nix::Result<(OwnedFd, OwnedFd)> {
    socketpair(
        AddressFamily::Unix,
        SockType::SeqPacket,
        None,
        SockFlag::empty(),
    )
}

#[cfg(test)]
mod tests {
    use std::io::IoSlice;
    use std::os::fd::AsFd;

    use super::send;
    use super::RequestOp;
    use crate::processors::peer::messages::create_socketpair;
    use crate::processors::peer::messages::send_str;

    #[test]
    fn roundtrip() {
        let (tx, rx) = create_socketpair().unwrap();
        send(
            tx.as_fd(),
            RequestOp::Process,
            [IoSlice::new(b"hello "), IoSlice::new(b"world")].into_iter(),
        )
        .unwrap();
        let mut buf = [0u8; 1024];
        let (op, msg) = super::recv_slice::<RequestOp>(rx.as_fd(), &mut buf).unwrap();
        assert_eq!(op, RequestOp::Process);
        assert_eq!(msg, b"hello world");
    }

    #[test]
    fn too_long() {
        let (tx, rx) = create_socketpair().unwrap();
        send_str(tx.as_fd(), RequestOp::Process, "hello world").unwrap();
        let mut buf = [0u8; 3];
        let err = super::recv_slice::<RequestOp>(rx.as_fd(), &mut buf).unwrap_err();
        assert_eq!(format!("{err}"), "message too long");
    }
}

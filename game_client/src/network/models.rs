use serde::{Serialize, Deserialize, Deserializer};
use zerocopy::{FromBytes, Immutable, KnownLayout};
use bincode::{config};
use anyhow::Result;

#[derive(Serialize, Deserialize, Debug, Clone)]
#[repr(C)]
pub struct Position {
    pub x: f32,
    pub y: f32,
    pub dx: f32,
    pub dy: f32
}

#[derive(Serialize, Deserialize, Debug)]
#[repr(C)]
pub struct PositionMessage {
    pub id: u32,
    pub position: Position
}

#[derive(Serialize, Debug)]
pub struct TcpRequest {
    pub opcode: u32
}

#[derive(Debug, FromBytes, Immutable, KnownLayout)]
#[repr(C, packed)]
pub struct TcpResponse {
    pub statuscode: u32,
    pub msg: [u8; 256]
}

#[derive(Serialize, Deserialize, Debug)]
#[repr(C)]
pub struct RegisterResponseMessage {
    pub id: u32
}

impl RegisterResponseMessage {
    pub fn from_tcp_response(tcp_response: TcpResponse) -> Result<RegisterResponseMessage> {
        let config = config::standard()
            .with_big_endian()
            .with_fixed_int_encoding();

        let (response, _len): (RegisterResponseMessage, usize) = bincode::serde::decode_from_slice(&tcp_response.msg, config).expect("failed to deserialize register response from TcpResponse");

        Ok(response) 
    }
}

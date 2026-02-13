use serde::{Serialize, Deserialize};
use zerocopy::{FromBytes, Immutable, KnownLayout};
use bincode::{config};
use bincode::de::{Decoder, Decode};
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

#[derive(Debug)]
pub struct GameStateMessage {
    pub seconds_to_start: i32,
    pub num_positions: u32,
    pub positions: Vec<Position>,
}

impl<Context> Decode<Context> for GameStateMessage {
    fn decode<D: Decoder<Context = Context>>(decoder: &mut D) -> Result<Self, bincode::error::DecodeError> {
        let seconds_to_start = i32::decode(decoder)?;
        let num_positions = u32::decode(decoder)?;
        let mut positions = Vec::with_capacity(num_positions as usize);
        for _ in 0..num_positions {
            let x = f32::decode(decoder)?;
            let y = f32::decode(decoder)?;
            let dx = f32::decode(decoder)?;
            let dy = f32::decode(decoder)?;
            positions.push(Position { x, y, dx, dy });
        }
        Ok(GameStateMessage { seconds_to_start, num_positions, positions })
    }
}

#[derive(FromBytes, Immutable, KnownLayout, Debug)]
#[repr(C, packed)]
pub struct TcpRequest {
    pub opcode: u32,
    pub msg: [u8; 256]
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

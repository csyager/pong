use serde::{Serialize, Deserialize, Deserializer};

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

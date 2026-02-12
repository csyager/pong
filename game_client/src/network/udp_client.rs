use tokio::net::UdpSocket;
use tokio::sync::Mutex;
use anyhow::Result;
use std::sync::Arc;
use std::net::SocketAddr;
use bincode::config;

use std::time::Instant;
use log::{info, error};

use super::models::{Position, PositionMessage};
use super::super::App;

pub struct UdpClient {
    pub socket: Arc<UdpSocket>,
    server_address: SocketAddr,
}

impl UdpClient {

    pub async fn connect() -> Result<UdpClient> {
        
        // 127.0.0.1:0 binds to an available local IP and auto-assigned port
        let socket = UdpSocket::bind("127.0.0.1:0").await?;

        let server_address: SocketAddr = "127.0.0.1:9034".parse()?;
        let client = UdpClient { socket: Arc::new(socket), server_address };
        
        Ok(client)

    }

    pub async fn send_position(&self, position: &Position, player_id: u32) -> Result<()> {
        let connection_message = PositionMessage {
            id: player_id,
            position: position.clone()
        };
        let config = config::standard()
            .with_big_endian()
            .with_fixed_int_encoding();

        let encoded_message = bincode::serde::encode_to_vec(&connection_message, config)?;
        info!("Sending position to socket.");
        self.socket.send_to(&encoded_message, self.server_address).await?;
        Ok(())
    }


    pub async fn listen(socket: Arc<UdpSocket>, app: Arc<Mutex<App>>) {
        let mut buf = [0u8; 1024];
        info!("Starting UDP listener loop.");
        loop {
            match socket.recv_from(&mut buf).await {
                Ok((len, addr)) => {
                    let config = config::standard()
                        .with_big_endian()
                        .with_fixed_int_encoding();

                    let (position_message, _len): (PositionMessage, usize) = bincode::serde::decode_from_slice(&buf, config).expect("failed to deserialize packet");
                    info!("Received position message for id {}: ({}, {}) with velocity vector ({}, {})", position_message.id, position_message.position.x, position_message.position.y, position_message.position.dx, position_message.position.dy);

                    let mut app = app.lock().await;
                    let now = Instant::now();
                    app.ping_ms = now.duration_since(app.last_udp_send).as_secs_f64() * 1000.0;
                    app.last_udp_recv = Some(now);

                    if position_message.id == 0 {
                        info!("Received position for ball.");
                        app.ball.x = position_message.position.x as f32;
                        app.ball.y = position_message.position.y as f32;
                        app.ball.dx = position_message.position.dx as f32;
                        app.ball.dy = position_message.position.dy as f32;
                    } else {
                        info!("Received position for player id {}.", position_message.id);
                        app.opponent.x = position_message.position.x as f32;
                        app.opponent.y = position_message.position.y as f32;
                        app.opponent.dx = position_message.position.dx as f32;
                        app.opponent.dy = position_message.position.dy as f32;
                    }
                }
                Err(e) => error!("Receiver error: {}", e)
            }
        }
    }

}

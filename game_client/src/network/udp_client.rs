use tokio::net::{UdpSocket};
use tokio::sync::Mutex;
use std::str;
use anyhow::Result;
use std::sync::Arc;
use std::net::SocketAddr;
use bincode::{config};

use log::{info, error};

use super::models::{PositionMessage};
use super::super::App;

pub struct UdpClient {
    socket: Arc<UdpSocket>,
    app: Arc<Mutex<App>>
}

impl UdpClient {

    pub async fn connect(app_mut: Arc<Mutex<App>>) -> Result<UdpClient> {
        
        // 127.0.0.1:0 binds to an available local IP and auto-assigned port
        let socket = UdpSocket::bind("127.0.0.1:0").await?;

        let server_address: SocketAddr = "127.0.0.1:9034".parse()?;

        
        let encoded_message = {
            let app = app_mut.lock().await;
            let connection_message = PositionMessage {
                id: 999,
                position: app.player.clone()
            };
            let config = config::standard()
                .with_big_endian()
                .with_fixed_int_encoding();

            bincode::serde::encode_to_vec(&connection_message, config)?
        };

        info!("Sending encoded position message: {:?}", encoded_message);

        socket.send_to(&encoded_message, server_address).await?;
        Ok(UdpClient { socket: Arc::new(socket), app: app_mut})

    }

    pub async fn listen(&self) {
        let mut buf = [0u8; 1024];
        loop {
            match self.socket.recv_from(&mut buf).await {
                Ok((len, addr)) => {
                    let config = config::standard()
                        .with_big_endian()
                        .with_fixed_int_encoding();

                    let (position_message, _len): (PositionMessage, usize) = bincode::serde::decode_from_slice(&buf, config).expect("failed to deserialize packet");
                    info!("Received position message for id {}: ({}, {}) with velocity vector ({}, {})", position_message.id, position_message.position.x, position_message.position.y, position_message.position.dx, position_message.position.dy);

                    let mut app = self.app.lock().await;

                    if position_message.id == 0 {
                        app.ball.x = position_message.position.x as f32;
                        app.ball.y = position_message.position.y as f32;
                        app.ball.dx = position_message.position.dx as f32;
                        app.ball.dy = position_message.position.dy as f32;
                    } else {
                        info!("Received position for player.");
                    }
                    
                }
                Err(e) => error!("Receiver error: {}", e)
            }
            
        }
    }

}

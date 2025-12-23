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

    pub async fn connect(app: Arc<Mutex<App>>) -> Result<UdpClient> {
        
        // 127.0.0.1:0 binds to an available local IP and auto-assigned port
        let socket = UdpSocket::bind("127.0.0.1:0").await?;

        let server_address: SocketAddr = "127.0.0.1:9034".parse()?;

        let connection_message = b"Hello!";

        socket.send_to(connection_message, server_address).await?;
        info!("Sent message: {}", str::from_utf8(connection_message).unwrap());

        Ok(UdpClient { socket: Arc::new(socket), app: app})

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
                    app.ball.x = position_message.position.x as f64;
                    app.ball.y = position_message.position.y as f64;
                    app.ball.dx = position_message.position.dx as f64;
                    app.ball.dy = position_message.position.dy as f64;
                    
                }
                Err(e) => error!("Receiver error: {}", e)
            }
            
        }
    }

}

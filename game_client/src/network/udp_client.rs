use tokio::net::UdpSocket;
use tokio::sync::Mutex;
use anyhow::Result;
use std::sync::Arc;
use std::net::SocketAddr;
use bincode::config;

use std::time::Instant;
use log::{info, error};

use super::models::{Position, GameStateMessage, PositionMessage};
use super::super::App;

pub struct UdpClient {
    pub socket: Arc<UdpSocket>,
    server_address: SocketAddr,
}

impl UdpClient {

    pub async fn connect(server_address: &str) -> Result<UdpClient> {
        
        // 0.0.0.0:0 binds to an available local IP and auto-assigned port
        let socket = UdpSocket::bind("0.0.0.0:0").await?;

        let server_address: SocketAddr = server_address.parse()?;
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

                    let (game_state_message, _len): (GameStateMessage, usize) = bincode::decode_from_slice(&buf[..len], config).expect("failed to deserialize packet");

                    let mut app = app.lock().await;
                    let now = Instant::now();
                    app.ping_ms = now.duration_since(app.last_udp_send).as_secs_f64() * 1000.0;
                    app.last_udp_recv = Some(now);

                    app.game_active = game_state_message.game_active;
                    app.seconds_to_start = game_state_message.seconds_to_start;
                    if app.player_id == 1 {
                        app.player_score = game_state_message.left_score;
                        app.opponent_score = game_state_message.right_score;
                    } else {
                        app.player_score = game_state_message.right_score;
                        app.opponent_score = game_state_message.left_score;
                    }
                    for (i, position) in game_state_message.positions.iter().enumerate() {
                        if i == 0 {
                            app.ball.x = position.x;
                            app.ball.y = position.y;
                            app.ball.dx = position.dx;
                            app.ball.dy = position.dy;
                        } else if i as u32 == app.player_id {
                            let dx = (app.player.x - position.x).abs();
                            let dy = (app.player.y - position.y).abs();
                            if dx > crate::RECONCILE_THRESHOLD || dy > crate::RECONCILE_THRESHOLD {
                                info!("Reconciling player position (drift: dx={}, dy={})", dx, dy);
                                app.player.x = position.x;
                                app.player.y = position.y;
                            }
                        } else {
                            app.opponent.x = position.x;
                            app.opponent.y = position.y;
                        }
                    }
                }
                Err(e) => error!("Receiver error: {}", e)
            }
        }
    }

}

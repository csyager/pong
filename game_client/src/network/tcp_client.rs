use tokio::sync::Mutex;
use anyhow::Result;
use std::sync::Arc;
use tokio::net::TcpStream;
use std::net::SocketAddr;
use zerocopy::FromBytes;

use log::{info, error};

use crate::App;
use super::models::{TcpRequest, TcpResponse};

pub struct TcpClient {
    pub stream: Arc<TcpStream>,
}

impl TcpClient {
    pub async fn connect(server_address: &str) -> Result<TcpClient> {

        let server_address: SocketAddr = server_address.parse()?;
        let stream = TcpStream::connect(server_address).await?;

        let client = TcpClient { stream: Arc::new(stream) };

        Ok(client)
    }

    pub async fn request(&mut self, request: &TcpRequest) -> Result<TcpResponse> {
        let mut request_buf = [0u8; 260];
        request_buf[0..4].copy_from_slice(&request.opcode.to_be_bytes());
        request_buf[4..260].copy_from_slice(&request.msg);

        info!("sending request to server.");
        loop {
            self.stream.writable().await?;
            match self.stream.try_write(&request_buf) {
                Ok(_) => break,
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => continue,
                Err(e) => return Err(e.into()),
            }
        }

        let mut buf = [0u8; 260];
        let mut total = 0;
        while total < buf.len() {
            self.stream.readable().await?;
            match self.stream.try_read(&mut buf[total..]) {
                Ok(0) => return Err(std::io::Error::new(std::io::ErrorKind::UnexpectedEof, "connection closed").into()),
                Ok(n) => total += n,
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => continue,
                Err(e) => return Err(e.into()),
            }
        }
        info!("read {} bytes", total);

        let response = TcpResponse::read_from_bytes(&buf).map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidData, "Buffer size mismatch"))?;
        Ok(response)
    }

    pub async fn listen(stream: Arc<TcpStream>) {
        let mut buf = [0u8; 260];
        info!("Starting TCP listener loop.");
        loop {
            // Wait until the stream is readable
            if let Err(e) = stream.readable().await {
                error!("TCP stream not readable: {}", e);
                break;
            }

            match stream.try_read(&mut buf) {
                Ok(0) => {
                    info!("TCP connection closed by server.");
                    break;
                }
                Ok(_n) => {
                    let request = match TcpRequest::read_from_bytes(&buf) {
                        Ok(req) => req,
                        Err(_) => {
                            error!("Failed to deserialize TcpResponse");
                            continue;
                        }
                    };
                    
                    Self::handle_tcp_event(request);
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                    // No data ready yet, loop back to readable()
                    continue;
                }
                Err(e) => {
                    error!("TCP Receiver error: {}", e);
                    break;
                }
            }
        }
    }

    async fn handle_tcp_event(request: TcpRequest) {
        match u32::from_be(request.opcode) {
            _ => {
                info!("Unknown opcode!")
            }
        }
    }
}

use tokio::sync::Mutex;
use anyhow::Result;
use std::sync::Arc;
use std::io::{self, Read, Write};
use tokio::net::TcpStream;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use std::net::{SocketAddr};
use bincode::{config};
use zerocopy::{FromBytes};

use log::{info, error};

use super::models::{TcpRequest, TcpResponse};
use super::super::App;

pub struct TcpClient {
    stream: TcpStream,
    app: Arc<Mutex<App>>
}

impl TcpClient {
    pub async fn connect(app_mut: Arc<Mutex<App>>) -> Result<TcpClient> {

        let server_address: SocketAddr = "127.0.0.1:9034".parse()?;
        let stream = TcpStream::connect(server_address).await?;

        let client = TcpClient { stream, app: app_mut };

        Ok(client)
    }

    pub async fn request(&mut self, request: &TcpRequest) -> Result<TcpResponse> {
        let config = config::standard()
            .with_big_endian()
            .with_fixed_int_encoding();

        let request_vec = bincode::serde::encode_to_vec(request, config)?;

        info!("sending request to server.");
        self.stream.write_all(&request_vec).await?;
        self.stream.flush().await?;

        let mut buf = [0u8; 260];
        let bytes_read = self.stream.read_exact(&mut buf).await?;
        info!("read {} bytes {:?}", bytes_read, buf);

        let response = TcpResponse::read_from_bytes(&buf).map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidData, "Buffer size mismatch"))?;       
        Ok(response)
    }
}

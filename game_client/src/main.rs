use ratatui::{
    layout::Rect,
    style::{Stylize, Color},
    symbols::border,
    text::{Line},
    layout::Alignment,
    widgets::{Block, Paragraph, Widget, canvas::{Canvas, Circle, Rectangle }},
    DefaultTerminal, TerminalOptions, Viewport, Frame
};
use crossterm::{
    event::{self, Event, EventStream, KeyCode, KeyEvent, KeyEventKind, PushKeyboardEnhancementFlags, KeyboardEnhancementFlags},
    execute,
    terminal::{EnterAlternateScreen }
};
use anyhow::Result;
use std::io::stdout;
use std::time::Instant;
use std::sync::Arc;
use tokio::sync::Mutex;
use tokio::time::{interval, Duration};
use futures::{StreamExt, FutureExt};


mod network;
use network::udp_client::UdpClient;
use network::tcp_client::TcpClient;
use network::models::{Position, TcpRequest, RegisterResponseMessage};

use log::{info};

const TICK_RATE: u64 = 16;  // ~60 fps

const COLS: u16 = 200;
const ROWS: u16 = 50;

const BALL_RADIUS: f32 = 2.0;
const PLAYER_LENGTH: f32 = 2.0;

const PLAYER_MOVE_SPEED: f32 = 5.0;

const ANTI_ALIASING_TIMEOUT: u64 = 350;

const SERVER_ADDRESS: &str = "127.0.0.1:9034";
const SERVER_TIMEOUT: Duration = Duration::from_secs(2);
const RECONCILE_THRESHOLD: f32 = 5.0;

#[derive(Debug)]
pub struct App {
    player_id: u32,
    player: Position, 
    opponent: Position,
    player_score: u8,
    opponent_score: u8,
    ball: Position,
    ball_dx: u8,
    ball_dy: u8,

    w_pressed: bool,
    w_last_seen: Instant,
    s_pressed: bool,
    s_last_seen: Instant,
    exit: bool,

    last_udp_send: Instant,
    last_udp_recv: Option<Instant>,
    ping_ms: f64,

    game_active: bool,
    seconds_to_start: i32,
    status_msg: String
}


impl App {

    fn new(player_id: u32) -> Self {
        let now = Instant::now();
        let right_position = Position {
            x: COLS as f32 - 10.0,
            y: ROWS as f32 / 2.0,
            dx: 0.0,
            dy: 0.0
        };
        let left_position = Position {
            x: 10.0,
            y: ROWS as f32 / 2.0,
            dx: 0.0,
            dy: 0.0
        };

        let player_position: Position;
        let opponent_position: Position;
        if player_id == 1 {
            player_position = left_position;
            opponent_position = right_position;
        } else {
            player_position = right_position;
            opponent_position = left_position;
        }
            
        Self {
            player_id: player_id,
            player: player_position,
            opponent: opponent_position,
            player_score: 0,
            opponent_score: 0,
            ball: Position {
                x: COLS as f32 / 2.0,
                y: ROWS as f32 / 2.0,
                dx: 5.0,
                dy: 5.0
            },
            ball_dx: 0,
            ball_dy: 0,

            w_pressed: false,
            w_last_seen: now,
            s_pressed: false,
            s_last_seen: now,
            exit: false,

            last_udp_send: now,
            last_udp_recv: None,
            ping_ms: 0.0,

            game_active: false,
            seconds_to_start: 0,
            status_msg: "Waiting on players...".to_string()
        }
    }

    // run the main loop until the user quits
    pub async fn run(state: Arc<Mutex<App>>, terminal: &mut DefaultTerminal, udp_client: Arc<Mutex<UdpClient>>) -> Result<()> {
        let mut events = EventStream::new();
        let mut ticker = interval(Duration::from_millis(TICK_RATE));
        let mut last_tick = Instant::now();

        loop {
            {
                if state.lock().await.exit { break; }
            }
            tokio::select! {
                _ = ticker.tick() => {
                    let now = Instant::now();
                    let delta_time = now.duration_since(last_tick).as_secs_f32();
                    last_tick = now;

                    let mut app = state.lock().await;
                    app.tick(delta_time, Arc::clone(&udp_client)).await?;
                    terminal.draw(|frame| app.draw(frame))?;
                }
                maybe_event = events.next().fuse() => {
                    if let Some(Ok(event)) = maybe_event {
                        let mut app = state.lock().await;
                        app.handle_event(event)?;
                    }
                }
            };
        }
        Ok(())
    }

    async fn tick(&mut self, delta_time: f32, udp_client: Arc<Mutex<UdpClient>>) -> Result<()> {
        let now = Instant::now();

        // always send position so the server learns our UDP address
        udp_client.lock().await.send_position(&self.player, self.player_id).await?;
        self.last_udp_send = Instant::now();

        if !self.game_active {
            if self.seconds_to_start > 0 {
                self.status_msg = format!("Starting in {}...", self.seconds_to_start);
                return Ok(())
            } else if self.last_udp_recv.is_none() {
                self.status_msg = "Waiting on players...".to_string();
                return Ok(())
            } else {
                // seconds_to_start hit 0 or went negative, game has started
                self.game_active = true;
                self.status_msg = "".to_string();
            }
        }

        let timeout = Duration::from_millis(ANTI_ALIASING_TIMEOUT);
        if self.s_pressed && now.duration_since(self.s_last_seen) > timeout {
            self.s_pressed = false;
        }
        if self.w_pressed && now.duration_since(self.w_last_seen) > timeout {
            self.w_pressed = false;
        }

        if self.s_pressed {
            self.player.y -= PLAYER_MOVE_SPEED * delta_time;
        }
        if self.w_pressed {
            self.player.y += PLAYER_MOVE_SPEED * delta_time;
        }

        if self.player.y >= ROWS.into() {
            self.player.y = ROWS.into();
        } else if self.player.y <= 0.0 {
            self.player.y = 0.0;
        }

        Ok(())
    }

    fn game_canvas(&self) -> impl Widget + '_ {

        let left_score; 
        let right_score;
        if self.player_id == 1 {
            left_score = format!(" Player: {}    ", self.player_score).green().bold();
            right_score = format!("    Opponent: {} ", self.opponent_score).red().bold();
        } else {
            left_score = format!(" Opponent: {}    ", self.opponent_score).red().bold();
            right_score = format!("    Player: {} ", self.player_score).green().bold();
        }
        let title = Line::from(vec![
            left_score, 
            "Pong".bold(),
            right_score
        ]);
        let server_status = match self.last_udp_recv {
            None => format!("{} (registered)", SERVER_ADDRESS).yellow(),
            Some(t) if Instant::now().duration_since(t) > SERVER_TIMEOUT => {
                format!("{} (no response)", SERVER_ADDRESS).red()
            }
            Some(_) => format!("{} ping {:.1}ms", SERVER_ADDRESS, self.ping_ms).green(),
        };

        let instructions = Line::from(vec![
            "Quit".into(),
            "<Q> ".blue().bold(),
            "Movement".into(),
            "<W><A><S><D> ".blue().bold(),
            server_status,
        ]);

        let block = Block::bordered()
            .title(title.centered())
            .title_bottom(instructions.centered())
            .border_set(border::THICK);


        Canvas::default()
            .block(block)
            .marker(ratatui::symbols::Marker::Braille)
            .paint(|ctx| {
                ctx.draw(&Circle {
                    x: self.ball.x as f64,
                    y: self.ball.y as f64,
                    radius: BALL_RADIUS as f64,
                    color: Color::Yellow
                });

                ctx.draw(&Rectangle {
                    x: self.player.x as f64,
                    y: self.player.y as f64,
                    width: PLAYER_LENGTH as f64,
                    height: PLAYER_LENGTH as f64,
                    color: Color::Green
                });

                ctx.draw(&Rectangle {
                    x: self.opponent.x as f64,
                    y: self.opponent.y as f64,
                    width: PLAYER_LENGTH as f64,
                    height: PLAYER_LENGTH as f64,
                    color: Color::Red
                });
            })
        .x_bounds([0.0, f64::from(COLS)])
        .y_bounds([0.0, f64::from(ROWS)])
    }


    fn draw(&self, frame: &mut Frame) {
        let area = Rect::new(0, 0, COLS, ROWS);
        let render_area = area.intersection(frame.area());

        frame.render_widget(self.game_canvas(), render_area);

        if !self.game_active {
            let overlay_width = 40;
            let overlay_height = 5;
            let overlay_area = Rect::new(
                render_area.x + (render_area.width - overlay_width) / 2,
                render_area.y + (render_area.height - overlay_height) / 2,
                overlay_width,
                overlay_height,
            );

            let overlay_block = Block::bordered()
                .border_set(border::DOUBLE)
                .style(ratatui::style::Style::default().bg(Color::Black));

            let msg = Paragraph::new(self.status_msg.clone())
                .alignment(Alignment::Center)
                .bold()
                .yellow()
                .block(overlay_block);

            frame.render_widget(msg, overlay_area);
        }
    }

    fn handle_event(&mut self, event: Event) -> Result<()> {
        match event {
            Event::Key(key_event) if key_event.kind == KeyEventKind::Press => {
                self.handle_key_event(key_event)
            }
            _ => {}
        };
        Ok(())
    }

    fn handle_key_event(&mut self, key_event: KeyEvent) {
        let now = Instant::now();
        match (key_event.code, key_event.kind) {
            (KeyCode::Char('q'), _) => self.exit(),
            (KeyCode::Char('w'), KeyEventKind::Press) => {
                self.w_pressed = true;
                self.w_last_seen = now;
            },
            (KeyCode::Char('s'), KeyEventKind::Press) => {
                self.s_pressed = true;
                self.s_last_seen = now;
            },
            _ => {}
        }
    }

    fn exit(&mut self) {
        self.exit = true;
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    // logging config
    log4rs::init_file("log4rs.yaml", Default::default()).unwrap();

    // networking configuration
    let udp_client = UdpClient::connect().await?;
    let mut tcp_client: TcpClient = TcpClient::connect().await?;

    // register with server
    info!("Registering with server.");
    let register_request = TcpRequest { opcode: 0, msg: [0; 256]};
    let tcp_response = tcp_client.request(&register_request).await?;
    let tcp_response_status = tcp_response.statuscode;
    info!("tcp_response status code: {}", tcp_response_status);
    let register_response = RegisterResponseMessage::from_tcp_response(tcp_response)?;

    info!("Registered with server, id = {}", register_response.id);

    // init game
    let app = Arc::new(Mutex::new(App::new(register_response.id)));

    let udp_client = Arc::new(Mutex::new(udp_client));

    // start UDP listener thread
    let listen_socket = Arc::clone(&udp_client.lock().await.socket);
    let listen_app = Arc::clone(&app);
    tokio::spawn(async move { UdpClient::listen(listen_socket, listen_app).await });

    // start TCP listener thread
    let tcp_stream = Arc::clone(&tcp_client.stream);
    let tcp_listen_app = Arc::clone(&app);
    tokio::spawn(async move { TcpClient::listen(tcp_stream, tcp_listen_app).await });
    execute!(stdout(), EnterAlternateScreen)?;

    let fixed_area = Rect::new(0, 0, COLS, ROWS);
    let mut terminal = ratatui::init_with_options(
        TerminalOptions {
            viewport: Viewport::Fixed(fixed_area)
        }
    );
    
    let result = App::run(app, &mut terminal, udp_client).await; 
    ratatui::restore();
    result
}


use ratatui::{
    layout::Rect,
    style::{Stylize, Color},
    symbols::border,
    text::{Line},
    widgets::{Block, Widget, canvas::{Canvas, Circle, Rectangle }},
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

const COLS: u16 = 300;
const ROWS: u16 = 50;

const BALL_RADIUS: f32 = 2.0;
const PLAYER_LENGTH: f32 = 2.0;

const PLAYER_MOVE_SPEED: f32 = 5.0;

const ANTI_ALIASING_TIMEOUT: u64 = 350;

#[derive(Debug)]
pub struct App {
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
    exit: bool
}


impl App {

    fn new() -> Self {
        let now = Instant::now();
        Self {
            player: Position {
                x: COLS as f32 - 10.0,
                y: ROWS as f32 / 2.0,
                dx: 0.0,
                dy: 0.0
            },
            opponent: Position {
                x: 10.0,
                y: ROWS as f32 / 2.0,
                dx: 0.0,
                dy: 0.0
            },
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
            exit: false
        }
    }

    // run the main loop until the user quits
    pub async fn run(state: Arc<Mutex<App>>, terminal: &mut DefaultTerminal) -> Result<()> {
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
                    app.tick(delta_time)?;
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

    fn tick(&mut self, delta_time: f32) -> Result<()> {
        // commenting out to rely on server physics
        // self.ball.x += self.ball.dx * delta_time;
        // self.ball.y += self.ball.dy * delta_time;

        // if self.ball.x - BALL_RADIUS <= 0.0 {
        //     self.ball.x = BALL_RADIUS;
        //     self.ball.dx *= -1.0;
        // } else if self.ball.x + BALL_RADIUS > COLS.into() {
        //     self.ball.x = COLS as f64 - BALL_RADIUS;
        //     self.ball.dx *= -1.0
        // }

        // if self.ball.y - BALL_RADIUS <= 0.0 {
        //     self.ball.y = BALL_RADIUS;
        //     self.ball.dy *= -1.0
        // } else if self.ball.y + BALL_RADIUS > ROWS.into() {
        //     self.ball.y = ROWS as f64 - BALL_RADIUS;
        //     self.ball.dy *= -1.0;
        // }
       
        let now = Instant::now();
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

        let title = Line::from("Terminal Game".bold());
        let instructions = Line::from(vec![
            "Quit".into(),
            "<Q> ".blue().bold(),
            "Movement".into(),
            "<W><A><S><D>".blue().bold()
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
    let app = Arc::new(Mutex::new(App::new()));

    // logging config
    log4rs::init_file("log4rs.yaml", Default::default()).unwrap();

    // networking
    let udp_client = UdpClient::connect(Arc::clone(&app)).await?;
    tokio::spawn(async move { udp_client.listen().await });
    
    let mut tcp_client: TcpClient = TcpClient::connect(Arc::clone(&app)).await?;
    
    // register with server
    let register_request = TcpRequest { opcode: 0 };
    let register_response = RegisterResponseMessage::from_tcp_response(
        tcp_client.request(&register_request).await?
    )?;

    info!("Registered with server, id = {}", register_response.id);

    execute!(stdout(), EnterAlternateScreen)?;

    let fixed_area = Rect::new(0, 0, COLS, ROWS);
    let mut terminal = ratatui::init_with_options(
        TerminalOptions {
            viewport: Viewport::Fixed(fixed_area)
        }
    );
    
    let result = App::run(app, &mut terminal).await; 
    ratatui::restore();
    result
}


# PONG

This repository contains a lightweight server application and a terminal-based client for playing multiplayer Pong.  The server is written in C, and the client in Rust using [Ratatui](https://ratatui.rs/) for the terminal interface.

## Installation

Download the source from this repository.  To build the game server natively, you'll need the following dependencies:
* make
* clang (or another compiler, just might need to tweak the Makefile)

Alternatively, you can use the Dockefile to build and run as a container:
`docker build -t pong_server:latest .`
`docker run -d -p 9034:9034/tcp -p 9034:9034/udp --name pong_server pong_server`


To build the game client, you'll need Rust and Cargo.  See the installation instructions here:  https://doc.rust-lang.org/cargo/getting-started/installation.html

To build the game client, run
```
cargo build --release
```

To build the server, run 
```
make
```

## Running a Game

On a network accessible host, run the game server:

```
./build/server
```

On each client, run the game interface from the terminal:

```
./target/release/game_client
```

//! The lobby built into the launcher: rampagelobby/src (Node) without Colyseus, answering the game exactly as it does.
pub mod addr;
pub mod games;
pub mod http;
#[cfg(test)]
mod games_tests;
#[cfg(test)]
mod http_tests;

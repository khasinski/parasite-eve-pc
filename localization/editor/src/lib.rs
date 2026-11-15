//! The Parasite Eve dialogue translation editor: the pack compiler it shares its rules with
//! `localization/pelang.py`, its exchange formats, and its desktop interface.
pub mod app;
pub mod build_catalog;
pub mod catalog;
pub mod cjk;
pub mod cyrillic;
pub mod dialogue_csv;
pub mod dialogue_search;
pub mod disc;
pub mod error;
pub mod gametext;
pub mod latin;
pub mod layout;
pub mod model;
pub mod pack;
pub mod pycompat;
pub mod pycsv;
pub mod roundtrip;
pub mod script;
pub mod validation;
pub mod wrap;

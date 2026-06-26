extern crate lix_doc;

mod generated {
    include!(concat!(env!("OUT_DIR"), "/generated.rs"));
}

mod ffi {
    use std::{
        error,
        fmt::{Debug, Display},
        slice::from_raw_parts,
    };

    pub unsafe fn from_raw_parts_u8<'a>(data: *const u8, length: usize) -> &'a [u8] {
        from_raw_parts(data, length)
    }

    pub(crate) use crate::generated::cpp::Error;

    impl Display for Error {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.write_str(&self.to_string())
        }
    }

    impl Debug for Error {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.write_str(&self.to_string())
        }
    }

    impl error::Error for Error {}
}

#[macro_use]
pub(crate) mod log {
    use std::fmt::{Debug, Display};

    static mut VERBOSITY: i32 = 99;

    // these MUST match the c++ struct exactly or log messages will be filtered incorrectly
    #[allow(unused)]
    pub const LVL_ERROR: i32 = 0;
    #[allow(unused)]
    pub const LVL_WARN: i32 = 1;
    #[allow(unused)]
    pub const LVL_NOTICE: i32 = 2;
    #[allow(unused)]
    pub const LVL_INFO: i32 = 3;
    #[allow(unused)]
    pub const LVL_TALKATIVE: i32 = 4;
    #[allow(unused)]
    pub const LVL_CHATTY: i32 = 5;
    #[allow(unused)]
    pub const LVL_DEBUG: i32 = 6;
    #[allow(unused)]
    pub const LVL_VOMIT: i32 = 7;

    pub struct Colorize<T: Display + Debug>(pub T);

    impl<T: Display + Debug> Display for Colorize<T> {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.write_str("\x1b[35;1m")?;
            Display::fmt(&self.0, f)?;
            f.write_str("\x1b[0m")
        }
    }

    impl<T: Display + Debug> Debug for Colorize<T> {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.write_str("\x1b[35;1m")?;
            Debug::fmt(&self.0, f)?;
            f.write_str("\x1b[0m")
        }
    }

    macro_rules! log_message {
        ( __format plain: $e:expr ) => {
            $e
        };
        ( __format: $e:expr ) => {
            crate::log::Colorize($e)
        };
        ( $level:expr, $str:expr $(, $(plain)? $item:tt)* $(,)? ) => {
            crate::generated::log_message(
                $level,
                &format!(
                    $str,
                    $( log_message!( __format: $item ) ),*
                )
            );
        };
    }

    #[macro_export]
    macro_rules! print_error {
        ( $str:expr $(, $( $rest:tt )* )? ) => { log_message!(crate::log::LVL_ERROR, $str $($($rest)*)*) }
    }
    #[macro_export]
    macro_rules! print_warning {
        ( $str:expr $(, $( $rest:tt )* )? ) => { log_message!(crate::log::LVL_WARN, $str $($($rest)*)*) }
    }
    #[macro_export]
    macro_rules! print_notice {
        ( $str:expr $(, $( $rest:tt )* )? ) => { log_message!(crate::log::LVL_NOTICE, $str $($($rest)*)*) }
    }
    #[macro_export]
    macro_rules! print_info {
        ( $str:expr $(, $( $rest:tt )* )? ) => { log_message!(crate::log::LVL_INFO, $str $($($rest)*)*) }
    }
    #[macro_export]
    macro_rules! print_talkative {
        ( $str:expr $(, $( $rest:tt )* )? ) => { log_message!(crate::log::LVL_TALKATIVE, $str $($($rest)*)*) }
    }
    #[macro_export]
    macro_rules! print_chatty {
        ( $str:expr $(, $( $rest:tt )* )? ) => { log_message!(crate::log::LVL_CHATTY, $str $($($rest)*)*) }
    }
    #[macro_export]
    macro_rules! print_debug {
        ( $str:expr $(, $( $rest:tt )* )? ) => { log_message!(crate::log::LVL_DEBUG, $str $($($rest)*)*) }
    }
    #[macro_export]
    macro_rules! print_vomit {
        ( $str:expr $(, $( $rest:tt )* )? ) => { log_message!(crate::log::LVL_VOMIT, $str $($($rest)*)*) }
    }

    #[macro_export]
    macro_rules! log_error {
        ( $str:literal $($rest:tt)* ) => {
            print_error!(concat!("\x1b[31;1merror:\x1b[0m ", $str), $($rest)*)
        }
    }
    #[macro_export]
    macro_rules! log_warning {
        ( $str:literal $($rest:tt)* ) => {
            print_warning!(concat!("\x1b[35;1mwarning:\x1b[0m ", $str), $($rest)*)
        }
    }

    pub(crate) mod ffi {
        pub unsafe fn set_verbosity(level: i32) {
            super::VERBOSITY = level;
        }
    }
}

mod ffi_test;

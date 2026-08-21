extern crate lix_doc;

#[cfg(test)]
#[macro_use]
extern crate assert_matches;

#[allow(clippy::all, clippy::restriction, deprecated)]
mod generated {
    include!(concat!(env!("OUT_DIR"), "/generated.rs"));
}

mod ffi {
    use std::{
        error,
        ffi::OsStr,
        fmt::{Debug, Display},
        os::unix::ffi::OsStrExt,
        slice::from_raw_parts,
    };

    pub unsafe fn from_raw_parts_u8<'a>(data: *const u8, length: usize) -> &'a [u8] {
        from_raw_parts(data, length)
    }

    pub unsafe fn to_os_str<'a>(data: *const u8, length: usize) -> &'a OsStr {
        OsStrExt::from_bytes(from_raw_parts(data, length))
    }

    pub(crate) fn get_cancel_signal() -> i32 {
        crate::UNSAFE_CANCELATION_SIGNAL as i32
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
    use std::{
        fmt::{Debug, Display},
        sync::atomic::AtomicI32,
    };

    pub(crate) static VERBOSITY: AtomicI32 = AtomicI32::new(99);

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

    pub struct Colorize<T>(pub T);

    impl<T: Display> Display for Colorize<T> {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.write_str("\x1b[35;1m")?;
            Display::fmt(&self.0, f)?;
            f.write_str("\x1b[0m")
        }
    }

    impl<T: Debug> Debug for Colorize<T> {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.write_str("\x1b[35;1m")?;
            Debug::fmt(&self.0, f)?;
            f.write_str("\x1b[0m")
        }
    }

    /// log a message at a given level with a given message and arguments.
    ///
    /// this macro is variadic and has a signature of `(i32, &str, impl Display
    /// ...)`. the string will be passed to `format!`, with each displayed item
    /// being passed in turn. all items passed will be colorized for display by
    /// default unless they are tagged with `@plain` *or* they are given inline
    /// in the format string (since we cannot inspect format string arguments).
    ///
    /// example:
    ///
    /// ```
    /// log_message!(0, "test");
    /// // yields a single message "test"
    /// log_message!(0, "{it} happened");
    /// // yields "it happened", fully uncolored
    /// log_message!(0, "arguments: {} {} {}", 1, @plain (|| 2)(), 3);
    /// // yields "arguments: 1 2 3", with the "1" and "3" colorized
    /// ```
    macro_rules! log_message {
        // during processing we hold an intermediate argument state of ($level, $str; __args [ ... ]; __format $infos).
        // infos are pulled out of the __format bit piece by piece to convert them into __args, all of which are passed
        // to `format!` once all infos have been processed. $infos itself is a comma-separated list of expressions that
        // may each be preceded by an `@plain` tag. `@plain` expressions are copied to __args as they were given, other
        // expressions are wrapped for colorization. once all infos are processed we format the message and pass it on.
        ( $level:expr, $str:expr $(, $($info:tt)* )? ) => {
            log_message!($level, $str ; __args []; __format $($($info)*)? )
        };

        ( $level:expr, $str:expr ; __args [ $($args:expr),* ]; __format $(,)? ) => {
            if $level <= crate::log::VERBOSITY.load(std::sync::atomic::Ordering::Relaxed) {
                crate::generated::log_message($level, &format!($str, $($args),*));
            }
        };
        ( $level:expr, $str:expr ; __args [ $($args:expr),* ]; __format @plain $e:expr $(, $($rest:tt)* )? ) => {
            log_message!($level, $str ; __args [ $($args,)* $e ]; __format $($($rest)*)* )
        };
        ( $level:expr, $str:expr ; __args [ $($args:expr),* ]; __format $e:expr $(, $($rest:tt)* )? ) => {
            log_message!($level, $str ; __args [ $($args,)* crate::log::Colorize($e) ]; __format $($($rest)*)? )
        };
    }

    /// forwards its arguments to `log_message!` at the `LVL_ERROR` level.
    #[macro_export]
    macro_rules! print_error {
        ( $str:expr $(, $( $rest:tt )*)? ) => { log_message!($crate::log::LVL_ERROR, $str, $($($rest)*)?) }
    }
    /// forwards its arguments to `log_message!` at the `LVL_WARN` level.
    #[macro_export]
    macro_rules! print_warning {
        ( $str:expr $(, $( $rest:tt )*)? ) => { log_message!($crate::log::LVL_WARN, $str, $($($rest)*)?) }
    }
    /// forwards its arguments to `log_message!` at the `LVL_NOTICE` level.
    #[macro_export]
    macro_rules! print_notice {
        ( $str:expr $(, $( $rest:tt )*)? ) => { log_message!($crate::log::LVL_NOTICE, $str, $($($rest)*)?) }
    }
    /// forwards its arguments to `log_message!` at the `LVL_INFO` level.
    #[macro_export]
    macro_rules! print_info {
        ( $str:expr $(, $( $rest:tt )*)? ) => { log_message!($crate::log::LVL_INFO, $str, $($($rest)*)?) }
    }
    /// forwards its arguments to `log_message!` at the `LVL_TALKATIVE` level.
    #[macro_export]
    macro_rules! print_talkative {
        ( $str:expr $(, $( $rest:tt )*)? ) => { log_message!($crate::log::LVL_TALKATIVE, $str, $($($rest)*)?) }
    }
    /// forwards its arguments to `log_message!` at the `LVL_CHATTY` level.
    #[macro_export]
    macro_rules! print_chatty {
        ( $str:expr $(, $( $rest:tt )*)? ) => { log_message!($crate::log::LVL_CHATTY, $str, $($($rest)*)?) }
    }
    /// forwards its arguments to `log_message!` at the `LVL_DEBUG` level.
    #[macro_export]
    macro_rules! print_debug {
        ( $str:expr $(, $( $rest:tt )*)? ) => { log_message!($crate::log::LVL_DEBUG, $str, $($($rest)*)?) }
    }
    /// forwards its arguments to `log_message!` at the `LVL_VOMIT` level.
    #[macro_export]
    macro_rules! print_vomit {
        ( $str:expr $(, $( $rest:tt )*)? ) => { log_message!($crate::log::LVL_VOMIT, $str, $($($rest)*)?) }
    }

    /// forwards its arguments to `print_error!` with a bold red `error: ` prepended to the message.
    #[macro_export]
    macro_rules! log_error {
        ( $str:literal $($rest:tt)* ) => {
            print_error!(concat!("\x1b[31;1merror:\x1b[0m ", $str) $($rest)*)
        }
    }
    /// forwards its arguments to `print_warning!` with a bold purple `warning: ` prepended to the message.
    #[macro_export]
    macro_rules! log_warning {
        ( $str:literal $($rest:tt)* ) => {
            print_warning!(concat!("\x1b[35;1mwarning:\x1b[0m ", $str) $($rest)*)
        }
    }

    pub(crate) mod ffi {
        pub unsafe fn set_verbosity(level: i32) {
            super::VERBOSITY.store(level, std::sync::atomic::Ordering::Relaxed)
        }
    }
}

pub(crate) mod embeds {
    pub fn buildenv_nix() -> &'static str {
        include_str!("../../legacy/buildenv.nix")
    }

    pub fn unpack_channel_nix() -> &'static str {
        include_str!("../../legacy/unpack-channel.nix")
    }

    pub fn generate_manpage_nix() -> &'static str {
        include_str!("../../../doc/manual/generate-manpage.nix")
    }

    pub fn get_env_sh() -> &'static str {
        include_str!("../../nix/get-env.sh")
    }

    pub fn profiles_md() -> &'static str {
        include_str!("../../../doc/manual/src/command-ref/files/profiles.md")
    }

    pub fn repl_overlays_nix() -> &'static str {
        include_str!("../../libcmd/repl-overlays.nix")
    }

    pub fn derivation_nix() -> &'static str {
        include_str!("../../libexpr/primops/derivation.nix")
    }

    pub fn imported_drv_to_derivation_nix() -> &'static str {
        include_str!("../../libexpr/imported-drv-to-derivation.nix")
    }

    pub fn fetchurl_nix() -> &'static str {
        include_str!("../../libexpr/fetchurl.nix")
    }
}

pub mod base64;
mod errors;
pub mod fetchers;
mod ffi_test;
pub mod fs;
pub mod futures;
mod machines;
mod repl;
pub mod sqlite;

#[cfg(test)]
mod test {
    #[test]
    #[allow(unreachable_code)]
    fn loggers_compile() {
        // we only want loggers to *compile*, we don't actually want to run them
        return;

        log_message!(0, "message");
        log_message!(0, "message",);
        log_message!(0, "message {}", "info");
        log_message!(0, "message {}", @plain "info");
        log_message!(0, "message {}", "info",);
        log_message!(0, "message {}", @plain "info",);
        log_message!(0, "message {} {}", "info", "info 2");
        log_message!(0, "message {} {}", "info", @plain "info 2");
        log_message!(0, "message {} {}", "info", "info 2",);
        log_message!(0, "message {} {}", "info", @plain "info 2",);
        log_message!(0, "message {} {} {}", "info", "info 1", "info 2");
        log_message!(0, "message {} {} {}", "info", "info 1", @plain "info 2");
        log_message!(0, "message {} {} {}", "info", "info 1", "info 2",);
        log_message!(0, "message {} {} {}", "info", "info 1", @plain "info 2",);

        log_error!("message");
        log_error!("message",);
        log_error!("message {}", "info");
        log_error!("message {}", @plain "info");
        log_error!("message {}", "info",);
        log_error!("message {}", @plain "info",);
        log_error!("message {} {}", "info", "info 2");
        log_error!("message {} {}", "info", @plain "info 2");
        log_error!("message {} {}", "info", "info 2",);
        log_error!("message {} {}", "info", @plain "info 2",);
        log_error!("message {} {} {}", "info", "info 1", "info 2");
        log_error!("message {} {} {}", "info", "info 1", @plain "info 2");
        log_error!("message {} {} {}", "info", "info 1", "info 2",);
        log_error!("message {} {} {}", "info", "info 1", @plain "info 2",);

        log_warning!("message");
        log_warning!("message",);
        log_warning!("message {}", "info");
        log_warning!("message {}", @plain "info");
        log_warning!("message {}", "info",);
        log_warning!("message {}", @plain "info",);
        log_warning!("message {} {}", "info", "info 2");
        log_warning!("message {} {}", "info", @plain "info 2");
        log_warning!("message {} {}", "info", "info 2",);
        log_warning!("message {} {}", "info", @plain "info 2",);
        log_warning!("message {} {} {}", "info", "info 1", "info 2");
        log_warning!("message {} {} {}", "info", "info 1", @plain "info 2");
        log_warning!("message {} {} {}", "info", "info 1", "info 2",);
        log_warning!("message {} {} {}", "info", "info 1", @plain "info 2",);

        // we'll just do one of these since they're all the same. Should Be Fine™
        print_error!("message");
        print_error!("message",);
        print_error!("message {}", "info");
        print_error!("message {}", @plain "info");
        print_error!("message {}", "info",);
        print_error!("message {}", @plain "info",);
        print_error!("message {} {}", "info", "info 2");
        print_error!("message {} {}", "info", @plain "info 2");
        print_error!("message {} {}", "info", "info 2",);
        print_error!("message {} {}", "info", @plain "info 2",);
        print_error!("message {} {} {}", "info", "info 1", "info 2");
        print_error!("message {} {} {}", "info", "info 1", @plain "info 2");
        print_error!("message {} {} {}", "info", "info 1", "info 2",);
        print_error!("message {} {} {}", "info", "info 1", @plain "info 2",);
    }
}

/// lix requires a reserved signal to cancel blocking syscalls in worker threads.
/// this signal **MUST NOT** be used by the application for *anything* other than
/// causing syscalls in threads to return with `EINTR`. your application *should*
/// configure this signal with an empty handler with the `SA_RESTART` flag unset.
/// lix will replace the handler each time it wants to cancel some operation, but
/// setting the handler early avoids surprises. the signal **MUST NOT** be masked
/// for cancelations to function as expected, however lixrs doesn't enforce this.
pub const UNSAFE_CANCELATION_SIGNAL: nix::sys::signal::Signal = cfg_select! {
    feature = "unsafe_cancel_with_usr1" => nix::sys::signal::Signal::SIGUSR1,
    _ => compile_error!("no cancel signal set!"),
};

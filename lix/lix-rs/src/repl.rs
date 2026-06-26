use std::{
    error::Error,
    path::{Path, PathBuf},
};

use rustyline::{
    completion::{extract_word, Completer},
    error::ReadlineError,
    history::FileHistory,
    CompletionType, Config, Editor,
};

use rustyline_derive::*;

pub(crate) use crate::generated::cpp::CxxCompleter;

pub(crate) trait ReplCompleter {
    fn complete(&self, input: &str) -> Vec<String>;
}

#[derive(Helper, Validator, Hinter, Highlighter)]
struct Helper<'a> {
    cxx: &'a CxxCompleter,
}

impl<'a> Completer for Helper<'a> {
    type Candidate = String;

    fn complete(
        &self,
        line: &str,
        pos: usize,
        _ctx: &rustyline::Context<'_>,
    ) -> rustyline::Result<(usize, Vec<Self::Candidate>)> {
        let line = &line[0..pos];
        if line == "" {
            Ok((0, vec![]))
        } else {
            let (start, token) = if line.starts_with(':') {
                (0, line)
            } else {
                // Same as editline's SEPS, except for double and single quotes:
                extract_word(line, pos, None, |c| {
                    "#$&()*:;<=>?[\\]^`{,}~\n\t ".contains(c)
                })
            };

            Ok((start, self.cxx.complete(token)))
        }
    }
}

#[derive(Debug)]
pub struct Rustyline<'a> {
    editor: Editor<Helper<'a>, FileHistory>,
    history_file: PathBuf,
}

impl<'a> Rustyline<'a> {
    pub fn new(
        history_file: impl AsRef<Path>,
        completer: &'a CxxCompleter,
    ) -> Result<Self, Box<dyn Error>> {
        let history_file = history_file.as_ref();

        // TODO add settings and stuff
        let config = Config::builder()
            .auto_add_history(true)
            .max_history_size(1000)?
            .history_ignore_dups(true)?
            .completion_type(CompletionType::List)
            .build();
        let history = FileHistory::with_config(&config);
        let mut editor = Editor::with_history(config, history)?;
        editor.set_helper(Some(Helper { cxx: completer }));
        // ignore history load errors for historical reasons
        // TODO stop that some day
        let _ = editor.load_history(history_file);
        Ok(Rustyline {
            editor,
            history_file: history_file.to_path_buf(),
        })
    }

    pub fn ask(&mut self, prompt: &str) -> Result<String, ReadlineError> {
        self.editor.readline(prompt)
    }

    pub fn write_history(&mut self) {
        if let Err(e) = self.editor.save_history(&self.history_file) {
            log_warning!("failed to write history file: {}", e);
        }
    }
}

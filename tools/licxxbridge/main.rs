use std::{fs, path::PathBuf};

use clap::{Parser, Subcommand};

#[derive(Parser)]
struct Args {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand, Clone)]
enum Command {
    Generate {
        #[arg(long = "in")]
        input: PathBuf,
        #[arg(long)]
        out_rs: PathBuf,
        #[arg(long)]
        out_header: PathBuf,
        #[arg(long)]
        out_shared_header: PathBuf,
        #[arg(long)]
        out_impl: PathBuf,
        #[arg(long)]
        out_deps: PathBuf,
    },
}

fn main() {
    let args = Args::parse();

    match args.command {
        Command::Generate {
            input,
            out_rs,
            out_header,
            out_shared_header,
            out_impl,
            out_deps,
        } => {
            // create an empty impl file if zngur doesn't expose anything to make life easier
            fs::write(&out_impl, "").expect("failed to touch impl file");

            zngur::Zngur::from_zng_file(input)
                .with_rs_file(out_rs)
                .with_cpp_file(out_impl)
                .with_h_file(&out_header)
                .with_crate_name("lix")
                .with_zng_header(&out_shared_header)
                .with_depfile(out_deps)
                .generate();

            let content = fs::read_to_string(&out_header).expect("failed to read generated header");
            fs::write(
                &out_header,
                content.replace(
                    "#include <zngur.h>",
                    &format!("#include \"{}\"", out_shared_header.display()),
                ),
            ).expect("failed to rewrite generated header");
        }
    }
}

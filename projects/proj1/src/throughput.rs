use std::env;
use std::fs::File;
use std::io;
// Files for Linux and Windows envs
#[cfg(unix)]
use std::os::unix::fs::FileExt as PlatformFileExt;

#[cfg(windows)]
use std::os::windows::fs::FileExt as PlatformFileExt;

// Time
use std::time::{Instant, Duration};

mod hash;
use hash::sum_block_hash;

// Reading based on OS
#[cfg(unix)]
fn read_block(
    file: &File,
    buffer: &mut [u8],
    offset: u64,
) -> io::Result<()> {
    file.read_exact_at(buffer, offset)
}


#[cfg(windows)]
fn read_block(
    file: &File,
    buffer: &mut [u8],
    offset: u64,
) -> io::Result<()> {
    use std::io::ErrorKind;

    let mut read = 0;

    while read < buffer.len() {
        let n = file.seek_read(&mut buffer[read..], offset + read as u64)?;

        if n == 0 {
            return Err(io::Error::new(
                ErrorKind::UnexpectedEof,
                "Unexpected end of file",
            ));
        }

        read += n;
    }

    Ok(())
}

const BLOCK_SIZE: u64 = 1048576;

fn main() {

    let args: Vec<String> = env::args().collect();
    if args.len() != 2 {
        println!("Usage: cargo run --release --bin latency filepath");
        return;
    }

    let file_res = File::open(args[1].clone());
    if file_res.is_err() {
        eprintln!("File not found: {}", args[1]);
        return;
    }
    let file = file_res.unwrap();

    let metadata = file.metadata();
    if metadata.is_err() {
        eprintln!("Error opening file: {}", args[1]);
        return;
    }
    let total_size = metadata.unwrap().len();
    let num_blocks = (total_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    let mut buffer = vec![0u8; BLOCK_SIZE as usize];
    let mut result = vec![0u8; 16];

    // Benchmark READ_PHASE
    let mut total_io_time = Duration::ZERO;
    // Benchmark READ + CPU Compute
    let read_start = Instant::now();

    for block_idx in 0..num_blocks
    {
        let offset = block_idx * BLOCK_SIZE;
        let bytes_to_read = std::cmp::min(BLOCK_SIZE, total_size - offset) as usize;

        // read
        let target_buf = &mut buffer[..bytes_to_read];
        let io_start = Instant::now();
        let err = read_block(
            &file,
            target_buf,
            offset,
        );
        total_io_time += io_start.elapsed();
        if err.is_err() {
            eprintln!("Error reading file: {}", err.unwrap_err());
            return;
        }

        // compute
        let block_hash = md5::compute(target_buf);
        sum_block_hash(&mut result, &block_hash);
    }

    let read_phase_time = read_start.elapsed();

    // Print measurements in a format convenient for collecting results
    println!("FILE={}", args[1]);
    println!("FILE_SIZE_BYTES={}", total_size);
    println!("BLOCK_SIZE={}", BLOCK_SIZE);
    println!("BLOCKS={}", num_blocks);
    println!(
        "READ_PHASE={:.3}",
        total_io_time.as_secs_f64() * 1000.0
    );
    println!(
        "READ+COMPUTE_PHASE={:.3}",
        read_phase_time.as_secs_f64() * 1000.0
    );

    print!("{} 0x", args[1].clone());
    for byte in &result {
        print!("{:02x}", byte);
    }
    println!("");

}
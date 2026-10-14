// Mmap
use memmap2::MmapOptions;

use std::env;
use std::fs::File;

// Blackbox
use std::hint::black_box;

// Time
use std::time::{Instant, Duration};

mod hash;
use hash::sum_block_hash;

const BLOCK_SIZE: usize = 65536;

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
    let total_size = metadata.unwrap().len() as usize;
    
    if total_size == 0 {
        eprintln!("File is empty");
        return;
    }
    
    let num_blocks = (total_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Maping the file using mmap
    let mmap = match unsafe { MmapOptions::new().map(&file) } {
        Ok(m) => m,
        Err(e) => {
            eprintln!("Error mapping file: {}", e);
            return;
        }
    };

    let mut result = vec![0u8; 16];

    // Benchmark READ_PHASE
    let mut total_io_time = Duration::ZERO;
    // Benchmark READ + CPU Compute
    let read_start = Instant::now();

    // Reading slices
    for chunk in mmap.chunks(BLOCK_SIZE) {
        let io_start = Instant::now();
        
        // read

        // The loop measures the I/O time
        // page size: 4096
        // touching 1 byte per OS memory page to trigger hardware
        // page faults and force the OS to fetch the page from disk/cache into RAM.

        // Using dummy blackbox so the compiler will not optimise the for loop
        // since black_box utilizes dummy it has to be preserved thus for loop must calculate the result
        let mut dummy: u8 = 0;
        for i in (0..chunk.len()).step_by(4096) {
            dummy = dummy.wrapping_add(chunk[i]);
        }
        black_box(dummy);
        total_io_time += io_start.elapsed();

        // compute
        let block_hash = md5::compute(chunk);
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
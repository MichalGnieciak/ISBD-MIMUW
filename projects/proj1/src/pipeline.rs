use std::env;
use std::fs::File;
use std::io::{self};
use std::sync::{mpsc, Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

#[cfg(unix)]
use std::os::unix::fs::FileExt as PlatformFileExt;

#[cfg(windows)]
use std::os::windows::fs::FileExt as PlatformFileExt;

mod hash;
use hash::sum_block_hash;
use hash::stretch_md5;

const BLOCK_SIZE: u64 = 1024 * 1024;
const K: u64 = 1;
// Additional params
const NUM_READERS: usize = 1;
const NUM_COMPUTE_THREADS: usize = 1;
const CHANNEL_CAPACITY: usize = 2;

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
        let n = file.seek_read(
            &mut buffer[read..],
            offset + read as u64,
        )?;

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

fn main() {

    // Benchmark TOTAL_MS: overall pipeline time
    let total_start = Instant::now();

    let args: Vec<String> = env::args().collect();
    if args.len() != 2 {
        println!("Usage: cargo run --release --bin pipeline filepath");
        return;
    }

    // Parameters check
    if BLOCK_SIZE == 0 || NUM_READERS == 0 || NUM_COMPUTE_THREADS == 0 || CHANNEL_CAPACITY == 0
    {
        eprintln!("Block size, thread counts and channel capacity must be positive");
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

    if total_size == 0 {
        eprintln!("File is empty");
        return;
    }

    let num_blocks = (total_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Bounded channel limits the number of queued blocks.
    let (tx, rx) = mpsc::sync_channel::<Result<Vec<u8>, String>>(CHANNEL_CAPACITY);
    // Defining readers
    let mut reader_handles = Vec::new();

    // Reading stage
    // Benchmark: READER_WALL_MS, calculates total time spend reading + channel blocks
    let reader_wall_start = Instant::now();
    for reader_idx in 0..NUM_READERS {
        let tx_reader = tx.clone();
        let filepath = args[1].clone();

        // Each raeder gets a continous range of blocks
        let start_block = num_blocks * reader_idx as u64 / NUM_READERS as u64;
        let end_block = num_blocks * (reader_idx + 1) as u64 / NUM_READERS as u64;

        let handle = thread::spawn(
            move || -> (u64, Duration) {
                
                // Defining parameters for read stage
                let file = match File::open(&filepath) {
                    Ok(file) => file,
                    Err(e) => {
                        let _ = tx_reader.send(Err(format!(
                            "Reader {} could not open file: {}",
                            reader_idx, e
                        )));
                        return (0, Duration::ZERO);
                    }
                };
                let mut bytes_read = 0u64;
                // Counting time spent on reads excluding waiting on blocked channel
                let mut io_time = Duration::ZERO;

                for block_idx in start_block..end_block {
                    let offset = block_idx * BLOCK_SIZE;
                    let bytes_to_read = std::cmp::min(BLOCK_SIZE, total_size - offset) as usize;

                    let mut block = vec![0u8; bytes_to_read];
                    
                    // read 
                    let io_start = Instant::now();
                    let read_result = read_block(&file, &mut block, offset);
                    io_time += io_start.elapsed();

                    if let Err(e) = read_result {
                        let _ = tx_reader.send(Err(format!(
                            "Reader {} failed at block {}: {}",
                            reader_idx, block_idx, e
                        )));
                        break;
                    }

                    bytes_read += bytes_to_read as u64;

                    // waits if the bounded channel is full.
                    if tx_reader.send(Ok(block)).is_err() {
                        break;
                    }
                }

                // Returning total time spent reading
                (bytes_read, io_time)
            },
        );

        reader_handles.push(handle);
    }
    drop(tx);

    // Multiple compute threads must share a receive channel
    // So receiving is done via a mutex
    let rx = Arc::new(Mutex::new(rx));

    let result = Arc::new(Mutex::new(vec![0u8; 16]));
    let error = Arc::new(Mutex::new(None::<String>));

    let mut compute_handles = Vec::new();
    // Benchmark: COMPUTE_WALL_MS, total time spend computing accumulative + channel blocks
    let compute_wall_start = Instant::now();
    for _ in 0..NUM_COMPUTE_THREADS {
        let rx_worker = Arc::clone(&rx);
        let result_worker = Arc::clone(&result);
        let error_worker = Arc::clone(&error);

        let handle = thread::spawn(move || -> (u64, Duration) {
            let mut blocks_processed = 0u64;
            // Coutning total time spent computing
            let mut compute_time = Duration::ZERO;

            loop {
                // Releasing the lock before performing the hash.
                let message = {
                    let receiver = rx_worker.lock().unwrap();
                    receiver.recv()
                };

                let block = match message {
                    // Block received
                    Ok(Ok(block)) => block,
                    // Sender error
                    Ok(Err(message)) => {
                        let mut shared_error =
                            error_worker.lock().unwrap();

                        if shared_error.is_none() {
                            *shared_error = Some(message);
                        }
                        continue;
                    }
                    // All sedners dropped
                    Err(_) => break, 
                };

                // compute
                // Counting compute time excluding waiting for received messages
                let compute_start = Instant::now();
                let block_hash = stretch_md5(&block, K);
                {
                    let mut shared_result = result_worker.lock().unwrap();
                    sum_block_hash(&mut shared_result, &block_hash);
                }

                compute_time += compute_start.elapsed();
                blocks_processed += 1;
            }

            // Returning total compute time
            (blocks_processed, compute_time)
        });

        compute_handles.push(handle);
    }

    // Waiting for the readers to finish
    let mut total_bytes_read = 0u64;
    // Benchmark: READ_CALL_MS, sums the durations of actual read calls across all reader threads
    let mut total_io_time = Duration::ZERO;
    let mut reader_panicked = false;

    for handle in reader_handles {
        match handle.join() {
            Ok((bytes, io_time)) => {
                // Gathering results
                total_bytes_read += bytes;
                total_io_time += io_time;
            }
            Err(_) => reader_panicked = true,
        }
    }

    let reader_wall_time = reader_wall_start.elapsed();
    // Waiting for all compute workers to finish
    let mut blocks_processed = 0u64;
    // Benchmark: COMPUTE_MS, total time of computation summed across all threads
    let mut total_compute_time = Duration::ZERO;
    let mut compute_panicked = false;

    for handle in compute_handles {
        match handle.join() {
            Ok((blocks, compute_time)) => {
                blocks_processed += blocks;
                total_compute_time += compute_time;
            }
            Err(_) => compute_panicked = true,
        }
    }

    let compute_wall_time = compute_wall_start.elapsed();
    let total_time = total_start.elapsed();

    if reader_panicked || compute_panicked {
        eprintln!("A reader or compute thread panicked");
        return;
    }

    if let Some(message) = error.lock().unwrap().as_ref() {
        eprintln!("{}", message);
        return;
    }

    let result = result.lock().unwrap();

    // Measurements
    println!("FILE={}", args[1]);
    println!("FILE_SIZE_BYTES={}", total_bytes_read);
    println!("BLOCK_SIZE={}", BLOCK_SIZE);
    println!("K={}", K);
    println!("READERS={}", NUM_READERS);
    println!("COMPUTE_THREADS={}", NUM_COMPUTE_THREADS);
    println!("CHANNEL_CAPACITY={}", CHANNEL_CAPACITY);
    println!("BLOCKS_EXPECTED={}", num_blocks);
    println!("BLOCKS_PROCESSED={}", blocks_processed);
    println!("READ_CALL_MS={:.3}", total_io_time.as_secs_f64() * 1000.0);
    println!("COMPUTE_MS={:.3}", total_compute_time.as_secs_f64() * 1000.0);
    println!("READER_WALL_MS={:.3}", reader_wall_time.as_secs_f64() * 1000.0);
    println!("COMPUTE_WALL_MS={:.3}", compute_wall_time.as_secs_f64() * 1000.0);
    println!("TOTAL_MS={:.3}", total_time.as_secs_f64() * 1000.0);
    print!("{} 0x", args[1].clone());
    for byte in result.iter() {
        print!("{:02x}", byte);
    }
    println!("");

}
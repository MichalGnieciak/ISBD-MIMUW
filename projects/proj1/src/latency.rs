use std::env;
use std::fs::File;
use std::io;
use rand::{rng, Rng, RngExt, SeedableRng};
use rand::rngs::StdRng;

// Files for Linux and Windows envs
#[cfg(unix)]
use std::os::unix::fs::FileExt as PlatformFileExt;

#[cfg(windows)]
use std::os::windows::fs::FileExt as PlatformFileExt;

// Channels
use std::sync:: {mpsc, Arc};

// Threads
use std::thread;

// Time
use std::time::Instant;

mod hash;
use hash::sum_block_hash;

pub struct RandomOrderIterator {
    elements: Vec<u64>,
    rng: Box<dyn Rng>
}

impl RandomOrderIterator {
    pub fn with_seed(size: u64, seed: u64) -> Self {
        Self { elements: (0 as u64..size).collect() , rng: Box::new(StdRng::seed_from_u64(seed)), }
    }

    pub fn new(size: u64) -> Self {
        Self {
            elements: (0..size).collect(),
            rng: Box::new(rng()),
        }
    }
}

impl Iterator for RandomOrderIterator {
    type Item = u64;

    fn next(&mut self) -> Option<Self::Item> {
        if self.elements.is_empty() {
            None
        } else {
            let rand_idx = self.rng.random_range(0..self.elements.len());
            Some(self.elements.swap_remove(rand_idx))
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.elements.len(), Some(self.elements.len()))
    }
}

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

const BLOCK_SIZE: u64 = 512;
const RAND_SEED: u64 = 1234;
// Number of threads, the program's parameter
const N_THREADS: usize = 16;

fn main() {

    // Benchmark TOTAL: Total runtime, including setup, reading and hashing
    let total_start = Instant::now();

    let args: Vec<String> = env::args().collect();
    if args.len() != 2 {
        println!("Usage: cargo run --release --bin latency filepath");
        return;
    }

    // Parameters validity check
    if N_THREADS == 0 || BLOCK_SIZE == 0 {
        eprintln!("Thread count and block size must be positive");
        return;
    }

    // Benchmark PREP_PHASE: Preparation time, including opening the file, reading its metadata
    let prep_start = Instant::now();


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
    let random_iter = RandomOrderIterator::with_seed(num_blocks, RAND_SEED);

    let mut result = vec![0u8; 16];
    
    // Gathering block indexes for each thread
    // First we distribute indexes, then each thread can work on its own
    let mut block_idxs = vec![Vec::new(); N_THREADS];

    // Distributing indexes among threads
    let mut nth_thread = 0;
    for block_idx in random_iter
    {
        block_idxs[nth_thread].push(block_idx);
        nth_thread = (nth_thread + 1) % N_THREADS;
    }

    let prep_time = prep_start.elapsed();

    // Share the file handle between reader threads.
    let file = Arc::new(file);

    // Opening a channel for threads to send their results after
    let (tx, rx) = mpsc::channel();

    // Creating threads based on the provided N_THREADS parameter
    let mut handles = vec![];

    // Benchmark READ_PHASE: Time for the reader phase, starts before reader threads are spawned.
    let read_start = Instant::now();

    for thread_blocks in block_idxs
    {    
        // Each thread gets their own file and channel copy
        let handle_file = Arc::clone(&file);
        let handle_tx = tx.clone();
       
        let handle = thread::spawn(move || {    
            let worker_start = Instant::now();        
            let mut buffer = vec![0u8; BLOCK_SIZE as usize];
            
            // Thread tries to read the next block_idx
            for block_idx in thread_blocks
            {
                let offset = block_idx * BLOCK_SIZE;
                let bytes_to_read = std::cmp::min(BLOCK_SIZE, total_size - offset) as usize;

                // read
                if let Err(e) = read_block(
                    &handle_file,
                    &mut buffer[..bytes_to_read],
                    offset,
                ) {
                    let _ = handle_tx.send(Err(format!(
                        "Error reading block {} at offset {}: {}",
                        block_idx, offset, e
                    )));
                    return worker_start.elapsed();
                }
                // Send the read part
                if handle_tx
                    .send(Ok(buffer[..bytes_to_read].to_vec()))
                    .is_err()
                {
                    return worker_start.elapsed();
                }
            }
            worker_start.elapsed()
        });
        // Adding the created thread
        handles.push(handle);
    }

    // Closing the sender
    drop(tx);

    // Flag for any error during execution
    let mut read_error: Option<String> = None;

    // Gathering results and computing final hash
    for message in rx {
        match message {
            Ok(block) => {
                let block_hash = md5::compute(&block);
                sum_block_hash(&mut result, &block_hash);
            }
            Err(err) => {
                if read_error.is_none() {
                    read_error = Some(err);
                }
            }
        }
    }

    // Wait for all readers and detect panic
    let mut worker_panicked = false;
    for handle in handles {
        if handle.join().is_err() {
            worker_panicked = true;
        }
    }

    let read_phase_time = read_start.elapsed();

    // Gathering any errors
    if let Some(err) = read_error {
        eprintln!("{}", err);
        return;
    }

    // Checking for thread panic
    if worker_panicked {
        eprintln!("A reader thread panicked");
        return;
    }

    let total_time = total_start.elapsed();

    // Print measurements in a format convenient for collecting results
    println!("FILE={}", args[1]);
    println!("FILE_SIZE_BYTES={}", total_size);
    println!("BLOCK_SIZE={}", BLOCK_SIZE);
    println!("THREADS={}", N_THREADS);
    println!("BLOCKS={}", num_blocks);
    println!("PREP_MS={:.3}", prep_time.as_secs_f64() * 1000.0);
    println!(
        "READ_PHASE_MS={:.3}",
        read_phase_time.as_secs_f64() * 1000.0
    );
    println!("TOTAL_MS={:.3}", total_time.as_secs_f64() * 1000.0);

    print!("{} 0x", args[1].clone());
    for byte in &result {
        print!("{:02x}", byte);
    }
    println!("");

}

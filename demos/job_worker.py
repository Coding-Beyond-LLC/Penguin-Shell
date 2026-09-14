#!/usr/bin/env python3
"""job_worker.py -- simulates one unit of work for the Penguin Shell jobs demo.

Usage: job_worker.py <job-number>

Pretends to process a batch of records and exits 0 on success or 1 on a
simulated failure, so the .psh script driving it has a real exit status to
branch on -- the same shape as a real batch job.
"""
import random
import sys
import time


def main():
    if len(sys.argv) != 2:
        print("usage: job_worker.py <job-number>", file=sys.stderr)
        return 2

    job_id = int(sys.argv[1])
    rng = random.Random(job_id)   # seeded by job id so a demo run repeats the same way every time
    record_count = rng.randint(200, 900)
    duration = rng.uniform(0.15, 0.4)

    print(f"[job {job_id:02d}] processing {record_count} records...")
    time.sleep(duration)

    if job_id % 4 == 0:            # jobs 4 and 8 simulate a bad batch
        print(f"[job {job_id:02d}] done in {duration:.2f}s -- FAILED (checksum mismatch)")
        return 1

    print(f"[job {job_id:02d}] done in {duration:.2f}s -- OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

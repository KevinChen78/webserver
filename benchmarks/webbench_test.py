#!/usr/bin/env python3
"""
Webbench-like HTTP server benchmark tool
Compatible with webbench command line interface
"""

import argparse
import http.client
import multiprocessing
import time
import sys
from urllib.parse import urlparse


def worker_process(args, result_queue):
    """Worker process that makes HTTP requests"""
    parsed = urlparse(args.url)
    host = parsed.hostname
    port = parsed.port or 80
    path = parsed.path or '/'
    if parsed.query:
        path += '?' + parsed.query

    success = 0
    failed = 0
    total_bytes = 0

    conn = http.client.HTTPConnection(host, port, timeout=5)

    start_time = time.time()
    end_time = start_time + args.time

    while time.time() < end_time:
        try:
            conn.request('GET', path)
            response = conn.getconn().getresponse()
            data = response.read()

            if response.status == 200:
                success += 1
                total_bytes += len(data)
            else:
                failed += 1

            if not args.keepalive:
                conn.close()
                conn = http.client.HTTPConnection(host, port, timeout=5)

        except Exception as e:
            failed += 1
            conn.close()
            conn = http.client.HTTPConnection(host, port, timeout=5)

    conn.close()

    result_queue.put({
        'success': success,
        'failed': failed,
        'bytes': total_bytes
    })


def main():
    parser = argparse.ArgumentParser(description='HTTP Server Benchmark Tool')
    parser.add_argument('-c', '--clients', type=int, default=100,
                        help='Number of concurrent clients')
    parser.add_argument('-t', '--time', type=int, default=30,
                        help='Test duration in seconds')
    parser.add_argument('--get', action='store_true',
                        help='Use GET request (default)')
    parser.add_argument('-k', '--keepalive', action='store_true',
                        help='Use HTTP keepalive')
    parser.add_argument('-q', '--quiet', action='store_true',
                        help='Quiet mode')
    parser.add_argument('url', help='Target URL')

    args = parser.parse_args()

    if not args.quiet:
        print(f"Webbench - Simple Web Benchmark {args.clients} clients, "
              f"running {args.time} sec.")
        print(f"\nBenchmarking: {args.url}")
        print(f"{args.clients} clients, running {args.time} sec.\n")

    result_queue = multiprocessing.Queue()
    processes = []

    # Start workers
    for i in range(args.clients):
        p = multiprocessing.Process(target=worker_process, args=(args, result_queue))
        p.start()
        processes.append(p)

    # Wait for completion
    for p in processes:
        p.join()

    # Collect results
    total_success = 0
    total_failed = 0
    total_bytes = 0

    for _ in range(args.clients):
        try:
            result = result_queue.get(timeout=1)
            total_success += result['success']
            total_failed += result['failed']
            total_bytes += result['bytes']
        except:
            pass

    # Calculate stats
    total_requests = total_success + total_failed
    qps = total_success / args.time
    speed = (total_bytes / args.time / 1024) if args.time > 0 else 0  # KB/s

    if not args.quiet:
        print(f"Speed={speed:.2f} pages/min, {speed * 60:.0f} bytes/sec.")
        print(f"Requests: {total_success} susceed, {total_failed} failed.")
        print(f"QPS: {qps:.2f} requests/sec")
    else:
        print(f"{qps:.2f}")

    return 0 if total_failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())

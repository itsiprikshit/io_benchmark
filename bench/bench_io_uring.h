#ifndef BENCH_IO_URING_H
#define BENCH_IO_URING_H

#include <sys/uio.h>
#include <sys/ioctl.h>
#include <stdlib.h>

#include <liburing.h>
#include "common.h"

#define QUEUE_DEPTH 128

struct readReqResp {
  uint64_t block_id; // Offset of block that was read.
  struct iovec iov; // Read Result.
  char buf[BS];
};

void read_iou(std::string filename, uint64_t file_size, json options) {
  uint64_t num_blocks = file_size / BS;
  struct io_uring ring;
  bool use_sqthread_poll = options["sqthread_poll"];
  bool use_sqthread_pin = options["sqthread_poll_pin"];

  struct io_uring_params params;
  memset(&params, 0, sizeof(params));
  if (use_sqthread_poll) {
    params.flags |= IORING_SETUP_SQPOLL;
  }
  if (use_sqthread_pin) {
    params.flags |= IORING_SETUP_SQ_AFF;
    params.sq_thread_cpu = 2;
  }


  io_uring_queue_init_params(QUEUE_DEPTH, &ring, &params);
  int fd = open(filename.c_str(), O_RDONLY | O_DIRECT);

  struct readReqResp *re = (struct readReqResp *)malloc(QUEUE_DEPTH * sizeof(readReqResp));
  for (int i=0; i < QUEUE_DEPTH; i++) {
    re[i].iov.iov_base = re[i].buf;
    re[i].iov.iov_len = BS;
    re[i].block_id = rand() % num_blocks;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_readv(sqe, fd, &(re[i].iov), 1, re[i].block_id * BS);
    io_uring_sqe_set_data(sqe, &re[i]);
    int ret = io_uring_submit(&ring);
    if (ret < 0) abort();
  }

  uint64_t num_blocks_read = 0;
  while (num_blocks_read < num_blocks) {
    struct io_uring_cqe *cqe;
    int ret = io_uring_peek_cqe(&ring, &cqe);
    if (ret == -EAGAIN) {
      continue;
    }
    if (ret < 0) {
      abort();
    }
    struct readReqResp *resp= (struct readReqResp *)io_uring_cqe_get_data(cqe);
    check_block((char *)resp->buf, resp->block_id);
    io_uring_cqe_seen(&ring, cqe);
    num_blocks_read++;

    // Let's add the resp back into the queue as a request.
    struct readReqResp *nextReq = resp;
    nextReq->block_id = rand() % num_blocks;
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_readv(sqe, fd, &(nextReq->iov), 1, nextReq->block_id * BS);
    io_uring_sqe_set_data(sqe, nextReq);
    ret = io_uring_submit(&ring);
    if (ret < 0) abort();
  }

  return;
}

void bench_iou(int num_workers, uint64_t file_size, json options) {
  std::vector<std::thread> threads;
  for (uint64_t i=0; i < num_workers; i++) {
    threads.push_back(std::thread(read_iou, getWorkerDataFileName(i), file_size, options));
  }
  for (uint64_t i=0; i < num_workers; i++) {
    threads[i].join();
  }
}

#endif

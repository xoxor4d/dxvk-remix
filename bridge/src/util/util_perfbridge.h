/*
 * Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#pragma once

// [PerfBridge]: per-interval bridge counters (docs/bridge-rework-study.md, M0).
// Enabled by bridge.conf "perfLogIntervalFrames = N" (0 = off, the default). Each side logs one line every N frames
// to its own log: the client to the bridge client log next to the game's d3d9.dll, the server to the server log in
// .trex. Only the device bridge is counted; the module bridge (IDirect3D9 calls) is start-up traffic.
//
// Cost when off: one relaxed atomic load per command. Cost when on: two QueryPerformanceCounter calls and a few
// relaxed atomic adds per command.

#include <atomic>
#include <cstdint>

namespace bridge_util {

  // Wait statistics of one end of a command ring (spin, then block on a wake event; util_atomiccircularqueue.h).
  // Cumulative; [PerfBridge] logs the deltas. Only the slow path (the condition was not met on entry) is counted.
  struct QueueWaitStats {
    std::atomic<uint64_t> waits { 0 };       // slow-path waits
    std::atomic<uint64_t> spinTicks { 0 };   // QPC ticks spent spinning: CPU time
    std::atomic<uint64_t> blockTicks { 0 };  // QPC ticks spent blocked on the wake event: no CPU
    std::atomic<uint64_t> blocks { 0 };      // event waits entered
    std::atomic<uint64_t> wakesSent { 0 };   // wake events this end signalled for the other end
  };

  namespace perf {
    static constexpr uint32_t kMaxCommands = 1024;

    extern std::atomic<uint32_t> g_intervalFrames;

    inline bool enabled() {
      return g_intervalFrames.load(std::memory_order_relaxed) != 0;
    }

    // Called once from Bridge<Device>::init with bridge.conf perfLogIntervalFrames.
    void init(uint32_t intervalFrames);

    // QueryPerformanceCounter ticks.
    int64_t now();

    // One command sent (client) or processed (server), with the data-ring words it carried and its duration.
    void noteCommand(uint16_t command, uint32_t dataWords, int64_t ticks);

    // Client: the most recent request, to which the next response wait is attributed.
    void noteSent(uint16_t command);

    // Client: time spent waiting for a server response (a blocking call).
    void noteResponseWait(int64_t ticks);

    // Client: time spent waiting on the Present semaphore.
    void notePresentWait(int64_t ticks);

    // Client: how one IDirect3DQuery9::GetData call was answered (A4, util_queryresults.h).
    enum class QueryPath : uint32_t {
      ClientEvent,   // event query answered from the server's processed-Issue counter, no IPC
      AsyncCached,   // result already in the shared slot (or cached), no IPC
      AsyncRequest,  // a non-blocking request was sent or is in flight; S_FALSE returned
      Blocking,      // old round trip (no slot, unsupported type, or client.asyncQueries = False)
      Count
    };
    void noteQuery(QueryPath path, bool signalled);

    // Buffer / texture / volume bytes uploaded (client: sent at Unlock; server: copied into the runtime).
    void noteUpload(uint64_t bytes);

    // Server: a create call failed while the client was not waiting for its result (async creates). Always counted
    // and reported, with or without perfLogIntervalFrames, so failures stay visible.
    void noteAsyncCreateFailure(uint16_t command, long hresult);

    // Once per frame on the thread that presents (client: the game's Present; server: the device thread).
    // writerFull: waits for room in the outgoing command ring; readerEmpty: waits for incoming commands.
    void onFrame(const QueueWaitStats& writerFull, const QueueWaitStats& readerEmpty);
  }
}

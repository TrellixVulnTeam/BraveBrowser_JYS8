// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/test_gpu_memory_buffer_manager.h"

#include <stddef.h>
#include <stdint.h>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/numerics/safe_conversions.h"
#include "ui/gfx/buffer_format_util.h"
#include "ui/gfx/gpu_memory_buffer.h"

namespace cc {
namespace {

class GpuMemoryBufferImpl : public gfx::GpuMemoryBuffer {
 public:
  GpuMemoryBufferImpl(TestGpuMemoryBufferManager* manager,
                      int id,
                      const gfx::Size& size,
                      gfx::BufferFormat format,
                      std::unique_ptr<base::SharedMemory> shared_memory,
                      size_t offset,
                      size_t stride)
      : manager_(manager),
        id_(id),
        size_(size),
        format_(format),
        shared_memory_(std::move(shared_memory)),
        offset_(offset),
        stride_(stride),
        mapped_(false) {}

  ~GpuMemoryBufferImpl() override { manager_->OnGpuMemoryBufferDestroyed(id_); }

  // Overridden from gfx::GpuMemoryBuffer:
  bool Map() override {
    DCHECK(!mapped_);
    DCHECK_EQ(stride_, gfx::RowSizeForBufferFormat(size_.width(), format_, 0));
    if (!shared_memory_->Map(offset_ +
                             gfx::BufferSizeForBufferFormat(size_, format_)))
      return false;
    mapped_ = true;
    return true;
  }
  void* memory(size_t plane) override {
    DCHECK(mapped_);
    DCHECK_LT(plane, gfx::NumberOfPlanesForBufferFormat(format_));
    return reinterpret_cast<uint8_t*>(shared_memory_->memory()) + offset_ +
           gfx::BufferOffsetForBufferFormat(size_, format_, plane);
  }
  void Unmap() override {
    DCHECK(mapped_);
    shared_memory_->Unmap();
    mapped_ = false;
  }
  gfx::Size GetSize() const override { return size_; }
  gfx::BufferFormat GetFormat() const override { return format_; }
  int stride(size_t plane) const override {
    DCHECK_LT(plane, gfx::NumberOfPlanesForBufferFormat(format_));
    return base::checked_cast<int>(gfx::RowSizeForBufferFormat(
        size_.width(), format_, static_cast<int>(plane)));
  }
  gfx::GpuMemoryBufferId GetId() const override { return id_; }
  gfx::GpuMemoryBufferHandle GetHandle() const override {
    gfx::GpuMemoryBufferHandle handle;
    handle.type = gfx::SHARED_MEMORY_BUFFER;
    handle.handle = shared_memory_->handle();
    handle.offset = base::checked_cast<uint32_t>(offset_);
    handle.stride = base::checked_cast<int32_t>(stride_);
    return handle;
  }
  ClientBuffer AsClientBuffer() override {
    return reinterpret_cast<ClientBuffer>(this);
  }

 private:
  TestGpuMemoryBufferManager* manager_;
  gfx::GpuMemoryBufferId id_;
  const gfx::Size size_;
  gfx::BufferFormat format_;
  std::unique_ptr<base::SharedMemory> shared_memory_;
  size_t offset_;
  size_t stride_;
  bool mapped_;
};

class GpuMemoryBufferFromClient : public gfx::GpuMemoryBuffer {
 public:
  GpuMemoryBufferFromClient(TestGpuMemoryBufferManager* manager,
                            int id,
                            gfx::GpuMemoryBuffer* client_buffer)
      : manager_(manager), id_(id), client_buffer_(client_buffer) {}

  ~GpuMemoryBufferFromClient() override {
    manager_->OnGpuMemoryBufferDestroyed(id_);
  }

  bool Map() override { return client_buffer_->Map(); }
  void* memory(size_t plane) override { return client_buffer_->memory(plane); }
  void Unmap() override { client_buffer_->Unmap(); }
  gfx::Size GetSize() const override { return client_buffer_->GetSize(); }
  gfx::BufferFormat GetFormat() const override {
    return client_buffer_->GetFormat();
  }
  int stride(size_t plane) const override {
    return client_buffer_->stride(plane);
  }
  gfx::GpuMemoryBufferId GetId() const override { return id_; }
  gfx::GpuMemoryBufferHandle GetHandle() const override {
    return client_buffer_->GetHandle();
  }
  ClientBuffer AsClientBuffer() override {
    return client_buffer_->AsClientBuffer();
  }

 private:
  TestGpuMemoryBufferManager* manager_;
  gfx::GpuMemoryBufferId id_;
  gfx::GpuMemoryBuffer* client_buffer_;
};

}  // namespace

TestGpuMemoryBufferManager::TestGpuMemoryBufferManager() {
}

TestGpuMemoryBufferManager::~TestGpuMemoryBufferManager() {
  DCHECK(buffers_.empty());
  DCHECK(clients_.empty());
  if (parent_gpu_memory_buffer_manager_)
    parent_gpu_memory_buffer_manager_->clients_.erase(client_id_);
}

std::unique_ptr<TestGpuMemoryBufferManager>
TestGpuMemoryBufferManager::CreateClientGpuMemoryBufferManager() {
  std::unique_ptr<TestGpuMemoryBufferManager> client(
      new TestGpuMemoryBufferManager);
  client->client_id_ = ++last_client_id_;
  client->parent_gpu_memory_buffer_manager_ = this;

  clients_[client->client_id_] = client.get();
  return client;
}

void TestGpuMemoryBufferManager::OnGpuMemoryBufferDestroyed(
    gfx::GpuMemoryBufferId gpu_memory_buffer_id) {
  DCHECK(buffers_.find(gpu_memory_buffer_id.id) != buffers_.end());
  buffers_.erase(gpu_memory_buffer_id.id);
}

std::unique_ptr<gfx::GpuMemoryBuffer>
TestGpuMemoryBufferManager::AllocateGpuMemoryBuffer(
    const gfx::Size& size,
    gfx::BufferFormat format,
    gfx::BufferUsage usage,
    gpu::SurfaceHandle surface_handle) {
  std::unique_ptr<base::SharedMemory> shared_memory(new base::SharedMemory);
  const size_t buffer_size = gfx::BufferSizeForBufferFormat(size, format);
  if (!shared_memory->CreateAnonymous(buffer_size))
    return nullptr;

  last_gpu_memory_buffer_id_ += 1;
  std::unique_ptr<gfx::GpuMemoryBuffer> result(new GpuMemoryBufferImpl(
      this, last_gpu_memory_buffer_id_, size, format, std::move(shared_memory),
      0, base::checked_cast<int>(
             gfx::RowSizeForBufferFormat(size.width(), format, 0))));
  buffers_[last_gpu_memory_buffer_id_] = result.get();
  return result;
}

std::unique_ptr<gfx::GpuMemoryBuffer>
TestGpuMemoryBufferManager::CreateGpuMemoryBufferFromHandle(
    const gfx::GpuMemoryBufferHandle& handle,
    const gfx::Size& size,
    gfx::BufferFormat format) {
  if (handle.type != gfx::SHARED_MEMORY_BUFFER)
    return nullptr;

  last_gpu_memory_buffer_id_ += 1;
  std::unique_ptr<gfx::GpuMemoryBuffer> result(new GpuMemoryBufferImpl(
      this, last_gpu_memory_buffer_id_, size, format,
      base::MakeUnique<base::SharedMemory>(handle.handle, false), handle.offset,
      handle.stride));
  buffers_[last_gpu_memory_buffer_id_] = result.get();
  return result;
}

void TestGpuMemoryBufferManager::SetDestructionSyncToken(
    gfx::GpuMemoryBuffer* buffer,
    const gpu::SyncToken& sync_token) {}

}  // namespace cc

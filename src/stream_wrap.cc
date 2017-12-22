// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "stream_wrap.h"
#include "stream_base-inl.h"

#include "env-inl.h"
#include "handle_wrap.h"
#include "node_buffer.h"
#include "node_counters.h"
#include "pipe_wrap.h"
#include "req_wrap-inl.h"
#include "tcp_wrap.h"
#include "udp_wrap.h"
#include "util-inl.h"

#include <stdlib.h>  // abort()
#include <string.h>  // memcpy()
#include <limits.h>  // INT_MAX


namespace node {

using v8::Context;
using v8::DontDelete;
using v8::EscapableHandleScope;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Local;
using v8::Object;
using v8::ReadOnly;
using v8::Signature;
using v8::Value;


void LibuvStreamWrap::Initialize(Local<Object> target,
                                 Local<Value> unused,
                                 Local<Context> context) {
  Environment* env = Environment::GetCurrent(context);

  const int kStreamWriteAsyncFlag = Environment::kStreamWriteAsyncFlag;
  const int kStreamWriteError = Environment::kStreamWriteError;
  const int kStreamDispatchedBytes = Environment::kStreamDispatchedBytes;

  target->Set(env->context(),
              FIXED_ONE_BYTE_STRING(env->isolate(), "writeInfoBuffer"),
              env->write_info_buffer().GetJSArray()).FromJust();
  NODE_DEFINE_CONSTANT(target, kStreamWriteAsyncFlag);
  NODE_DEFINE_CONSTANT(target, kStreamWriteError);
  NODE_DEFINE_CONSTANT(target, kStreamDispatchedBytes);
}


LibuvStreamWrap::LibuvStreamWrap(Environment* env,
                                 Local<Object> object,
                                 uv_stream_t* stream,
                                 AsyncWrap::ProviderType provider)
    : HandleWrap(env,
                 object,
                 reinterpret_cast<uv_handle_t*>(stream),
                 provider),
      StreamBase(env),
      stream_(stream) {
}


void LibuvStreamWrap::AddMethods(Environment* env,
                                 v8::Local<v8::FunctionTemplate> target,
                                 int flags) {
  Local<FunctionTemplate> get_write_queue_size =
      FunctionTemplate::New(env->isolate(),
                            GetWriteQueueSize,
                            env->as_external(),
                            Signature::New(env->isolate(), target));
  target->PrototypeTemplate()->SetAccessorProperty(
      env->write_queue_size_string(),
      get_write_queue_size,
      Local<FunctionTemplate>(),
      static_cast<PropertyAttribute>(ReadOnly | DontDelete));
  env->SetProtoMethod(target, "setBlocking", SetBlocking);
  StreamBase::AddMethods<LibuvStreamWrap>(env, target, flags);
}


int LibuvStreamWrap::GetFD() {
  int fd = -1;
#if !defined(_WIN32)
  if (stream() != nullptr)
    uv_fileno(reinterpret_cast<uv_handle_t*>(stream()), &fd);
#endif
  return fd;
}


bool LibuvStreamWrap::IsAlive() {
  return HandleWrap::IsAlive(this);
}


bool LibuvStreamWrap::IsClosing() {
  return uv_is_closing(reinterpret_cast<uv_handle_t*>(stream()));
}


AsyncWrap* LibuvStreamWrap::GetAsyncWrap() {
  return static_cast<AsyncWrap*>(this);
}


bool LibuvStreamWrap::IsIPCPipe() {
  return is_named_pipe_ipc();
}


int LibuvStreamWrap::ReadStart() {
  return uv_read_start(stream(), [](uv_handle_t* handle,
                                    size_t suggested_size,
                                    uv_buf_t* buf) {
    static_cast<LibuvStreamWrap*>(handle->data)->OnUvAlloc(suggested_size, buf);
  }, [](uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    static_cast<LibuvStreamWrap*>(stream->data)->OnUvRead(nread, buf);
  });
}


int LibuvStreamWrap::ReadStop() {
  return uv_read_stop(stream());
}


void LibuvStreamWrap::OnUvAlloc(size_t suggested_size, uv_buf_t* buf) {
  HandleScope scope(env()->isolate());
  Context::Scope context_scope(env()->context());

  *buf = EmitAlloc(suggested_size);
}



template <class WrapType, class UVType>
static Local<Object> AcceptHandle(Environment* env, LibuvStreamWrap* parent) {
  EscapableHandleScope scope(env->isolate());
  Local<Object> wrap_obj;
  UVType* handle;

  wrap_obj = WrapType::Instantiate(env, parent, WrapType::SOCKET);
  if (wrap_obj.IsEmpty())
    return Local<Object>();

  WrapType* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, wrap_obj, Local<Object>());
  handle = wrap->UVHandle();

  if (uv_accept(parent->stream(), reinterpret_cast<uv_stream_t*>(handle)))
    ABORT();

  return scope.Escape(wrap_obj);
}


void LibuvStreamWrap::OnUvRead(ssize_t nread, const uv_buf_t* buf) {
  HandleScope scope(env()->isolate());
  Context::Scope context_scope(env()->context());
  uv_handle_type type = UV_UNKNOWN_HANDLE;

  if (is_named_pipe_ipc() &&
      uv_pipe_pending_count(reinterpret_cast<uv_pipe_t*>(stream())) > 0) {
    type = uv_pipe_pending_type(reinterpret_cast<uv_pipe_t*>(stream()));
  }

  // We should not be getting this callback if someone as already called
  // uv_close() on the handle.
  CHECK_EQ(persistent().IsEmpty(), false);

  if (nread > 0) {
    if (is_tcp()) {
      NODE_COUNT_NET_BYTES_RECV(nread);
    } else if (is_named_pipe()) {
      NODE_COUNT_PIPE_BYTES_RECV(nread);
    }

    Local<Object> pending_obj;

    if (type == UV_TCP) {
      pending_obj = AcceptHandle<TCPWrap, uv_tcp_t>(env(), this);
    } else if (type == UV_NAMED_PIPE) {
      pending_obj = AcceptHandle<PipeWrap, uv_pipe_t>(env(), this);
    } else if (type == UV_UDP) {
      pending_obj = AcceptHandle<UDPWrap, uv_udp_t>(env(), this);
    } else {
      CHECK_EQ(type, UV_UNKNOWN_HANDLE);
    }

    if (!pending_obj.IsEmpty()) {
      object()->Set(env()->context(),
                    env()->pending_handle_string(),
                    pending_obj).FromJust();
    }
  }

  EmitRead(nread, *buf);
}


void LibuvStreamWrap::GetWriteQueueSize(
    const FunctionCallbackInfo<Value>& info) {
  LibuvStreamWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, info.This());

  if (wrap->stream() == nullptr) {
    info.GetReturnValue().Set(0);
    return;
  }

  uint32_t write_queue_size = wrap->stream()->write_queue_size;
  info.GetReturnValue().Set(write_queue_size);
}


void LibuvStreamWrap::SetBlocking(const FunctionCallbackInfo<Value>& args) {
  LibuvStreamWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.Holder());

  CHECK_GT(args.Length(), 0);
  if (!wrap->IsAlive())
    return args.GetReturnValue().Set(UV_EINVAL);

  bool enable = args[0]->IsTrue();
  args.GetReturnValue().Set(uv_stream_set_blocking(wrap->stream(), enable));
}


int LibuvStreamWrap::DoShutdown() {
  return uv_shutdown(&req_.shutdown,
                     stream(), [](uv_shutdown_t* req, int status) {
  LibuvStreamWrap* stream =
      ContainerOf(&LibuvStreamWrap::req_,
                  static_cast<generic_stream_request*>(
                      ContainerOf(&generic_stream_request::shutdown, req)));
    Environment* env = stream->env();

    HandleScope handle_scope(env->isolate());
    Context::Scope context_scope(env->context());

    stream->AfterShutdown(status);
  });
}


// NOTE: Call to this function could change both `buf`'s and `count`'s
// values, shifting their base and decrementing their length. This is
// required in order to skip the data that was successfully written via
// uv_try_write().
int LibuvStreamWrap::DoTryWrite(uv_buf_t** bufs, size_t* count) {
  int err;
  size_t written;
  uv_buf_t* vbufs = *bufs;
  size_t vcount = *count;

  err = uv_try_write(stream(), vbufs, vcount);
  if (err == UV_ENOSYS || err == UV_EAGAIN)
    return 0;
  if (err < 0)
    return err;

  // Slice off the buffers: skip all written buffers and slice the one that
  // was partially written.
  written = err;
  for (; vcount > 0; vbufs++, vcount--) {
    // Slice
    if (vbufs[0].len > written) {
      vbufs[0].base += written;
      vbufs[0].len -= written;
      written = 0;
      break;

    // Discard
    } else {
      written -= vbufs[0].len;
    }
  }

  *bufs = vbufs;
  *count = vcount;

  return 0;
}


int LibuvStreamWrap::DoWrite(uv_buf_t* bufs,
                             size_t count,
                             uv_stream_t* send_handle) {
  int r;
  if (send_handle == nullptr) {
    r = uv_write(&req_.write, stream(), bufs, count, AfterUvWrite);
  } else {
    r = uv_write2(&req_.write, stream(), bufs, count, send_handle,
                  AfterUvWrite);
  }

  if (!r) {
    size_t bytes = 0;
    for (size_t i = 0; i < count; i++)
      bytes += bufs[i].len;
    if (stream()->type == UV_TCP) {
      NODE_COUNT_NET_BYTES_SENT(bytes);
    } else if (stream()->type == UV_NAMED_PIPE) {
      NODE_COUNT_PIPE_BYTES_SENT(bytes);
    }
  }

  return r;
}


void LibuvStreamWrap::AfterUvWrite(uv_write_t* req, int status) {
  LibuvStreamWrap* stream =
      ContainerOf(&LibuvStreamWrap::req_,
                  static_cast<generic_stream_request*>(
                      ContainerOf(&generic_stream_request::write, req)));

  stream->AfterWrite(status);
}


}  // namespace node

NODE_MODULE_CONTEXT_AWARE_INTERNAL(stream_wrap,
                                   node::LibuvStreamWrap::Initialize)

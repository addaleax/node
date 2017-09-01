#ifndef SRC_NODE_WORKER_H_
#define SRC_NODE_WORKER_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "node_messaging.h"
#include <unordered_map>

namespace node {
namespace worker {

// A worker thread, as represented in its parent thread.
class Worker : public AsyncWrap {
 public:
  Worker(Environment* env, v8::Local<v8::Object> wrap);
  ~Worker();

  // Run the worker. This is only called from the worker thread.
  void Run();

  // Forcibly exit the thread with a specified exit code. This may be called
  // from any thread.
  void Exit(int code);

  // Wait for the worker thread to stop (in a blocking manner).
  void JoinThread();

  size_t self_size() const override;
  bool is_stopped() const;

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void StartThread(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void StopThread(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetMessagePort(const v8::FunctionCallbackInfo<v8::Value>& args);

  void OnThreadStopped();
  void DisposeIsolate();

  void FatalError(const char* location, const char* message);

  static Worker* ForIsolate(v8::Isolate* isolate);

 private:
  uv_loop_t loop_;
  DeleteFnPtr<IsolateData, FreeIsolateData> isolate_data_;
  DeleteFnPtr<Environment, FreeEnvironment> env_;
  v8::Isolate* isolate_ = nullptr;
  DeleteFnPtr<ArrayBufferAllocator, FreeArrayBufferAllocator>
      array_buffer_allocator_;
  uv_thread_t tid_;

  // This mutex protects access to all variables listed below it.
  mutable Mutex mutex_;
  // This only protect stopped_. If both locks are acquired, this needs to
  // be the latter one.
  mutable Mutex stopped_mutex_;
  bool stopped_ = true;
  bool thread_joined_ = true;
  int exit_code_ = 0;
  double thread_id_ = -1;

  std::unique_ptr<MessagePortData> child_port_data_;

  // The child port is always kept alive by the child Environment's persistent
  // handle to it.
  MessagePort* child_port_ = nullptr;
  // This is always kept alive because the JS object associated with the Worker
  // instance refers to it via its MessagePort property.
  MessagePort* parent_port_ = nullptr;

  static Mutex by_isolate_mutex_;
  static std::unordered_map<v8::Isolate*, Worker*> by_isolate_;
};

}  // namespace worker
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS


#endif  // SRC_NODE_WORKER_H_

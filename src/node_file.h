#ifndef SRC_NODE_FILE_H_
#define SRC_NODE_FILE_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "node.h"
#include "stream_base.h"
#include "req_wrap-inl.h"

namespace node {

using v8::Context;
using v8::FunctionCallbackInfo;
using v8::HandleScope;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::Promise;
using v8::Undefined;
using v8::Value;

namespace fs {


class FSReqBase : public ReqWrap<uv_fs_t> {
 public:
  FSReqBase(Environment* env, Local<Object> req, AsyncWrap::ProviderType type)
      : ReqWrap(env, req, type) {
    Wrap(object(), this);
  }

  virtual ~FSReqBase() {
    ClearWrap(object());
  }

  void Init(const char* syscall,
            const char* data = nullptr,
            size_t len = 0,
            enum encoding encoding = UTF8) {
    syscall_ = syscall;
    encoding_ = encoding;

    if (data != nullptr) {
      CHECK(!has_data_);
      buffer_.AllocateSufficientStorage(len + 1);
      buffer_.SetLengthAndZeroTerminate(len);
      memcpy(*buffer_, data, len);
      has_data_ = true;
    }
  }

  virtual void FillStatsArray(const uv_stat_t* stat) = 0;
  virtual void Reject(Local<Value> reject) = 0;
  virtual void Resolve(Local<Value> value) = 0;
  virtual void ResolveStat() = 0;
  virtual void SetReturnValue(const FunctionCallbackInfo<Value>& args) = 0;

  const char* syscall() const { return syscall_; }
  const char* data() const { return has_data_ ? *buffer_ : nullptr; }
  enum encoding encoding() const { return encoding_; }

  size_t self_size() const override { return sizeof(*this); }

 private:
  enum encoding encoding_ = UTF8;
  bool has_data_ = false;
  const char* syscall_ = nullptr;

  // Typically, the content of buffer_ is something like a file name, so
  // something around 64 bytes should be enough.
  MaybeStackBuffer<char, 64> buffer_;

  DISALLOW_COPY_AND_ASSIGN(FSReqBase);
};

class FSReqWrap : public FSReqBase {
 public:
  FSReqWrap(Environment* env, Local<Object> req)
      : FSReqBase(env, req, AsyncWrap::PROVIDER_FSREQWRAP) { }

  void FillStatsArray(const uv_stat_t* stat) override;
  void Reject(Local<Value> reject) override;
  void Resolve(Local<Value> value) override;
  void ResolveStat() override;
  void SetReturnValue(const FunctionCallbackInfo<Value>& args) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(FSReqWrap);
};

class FSReqPromise : public FSReqBase {
 public:
  explicit FSReqPromise(Environment* env);

  ~FSReqPromise() override;

  void FillStatsArray(const uv_stat_t* stat) override;
  void Reject(Local<Value> reject) override;
  void Resolve(Local<Value> value) override;
  void ResolveStat() override;
  void SetReturnValue(const FunctionCallbackInfo<Value>& args) override;

 private:
  bool finished_ = false;
  AliasedBuffer<double, v8::Float64Array> stats_field_array_;
  DISALLOW_COPY_AND_ASSIGN(FSReqPromise);
};

class FSReqAfterScope {
 public:
  FSReqAfterScope(FSReqBase* wrap, uv_fs_t* req);
  ~FSReqAfterScope();

  bool Proceed();

  void Reject(uv_fs_t* req);

 private:
  FSReqBase* wrap_ = nullptr;
  uv_fs_t* req_ = nullptr;
  HandleScope handle_scope_;
  Context::Scope context_scope_;
};

class FileHandle;

// A request wrap specifically for uv_fs_read()s scheduled for reading
// from a FileHandle.
class FileHandleReadWrap : public ReqWrap<uv_fs_t> {
 public:
  FileHandleReadWrap(FileHandle* handle, v8::Local<v8::Object> obj);

  static inline FileHandleReadWrap* from_req(uv_fs_t* req) {
    return static_cast<FileHandleReadWrap*>(ReqWrap::from_req(req));
  }

  size_t self_size() const override { return sizeof(*this); }

 private:
  FileHandle* file_handle_;
  uv_buf_t buffer_;

  friend class FileHandle;
};

// A wrapper for a file descriptor that will automatically close the fd when
// the object is garbage collected
class FileHandle : public AsyncWrap, public StreamBase {
 public:
  FileHandle(Environment* env,
             int fd,
             v8::Local<v8::Object> obj = v8::Local<v8::Object>());
  virtual ~FileHandle();

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);

  int fd() const { return fd_; }
  size_t self_size() const override { return sizeof(*this); }

  // Will asynchronously close the FD and return a Promise that will
  // be resolved once closing is complete.
  static void Close(const FunctionCallbackInfo<Value>& args);

  // Releases ownership of the FD.
  static void ReleaseFD(const FunctionCallbackInfo<Value>& args);

  // StreamBase interface:
  int ReadStart() override;
  int ReadStop() override;

  bool IsAlive() override { return !closed_; }
  bool IsClosing() override { return closing_; }
  AsyncWrap* GetAsyncWrap() override { return this; }

  // In the case of file streams, shutting down corresponds to closing.
  ShutdownWrap* CreateShutdownWrap(v8::Local<v8::Object> object) override;
  int DoShutdown(ShutdownWrap* req_wrap) override;

  int DoWrite(WriteWrap* w,
              uv_buf_t* bufs,
              size_t count,
              uv_stream_t* send_handle) override {
    return UV_ENOSYS;  // Not implemented (yet).
  }

 private:
  // Synchronous close that emits a warning
  void Close();
  void AfterClose();

  class CloseReq : public ReqWrap<uv_fs_t> {
   public:
    CloseReq(Environment* env,
             Local<Promise> promise,
             Local<Value> ref)
        : ReqWrap(env,
                  env->fdclose_constructor_template()
                      ->NewInstance(env->context()).ToLocalChecked(),
                  AsyncWrap::PROVIDER_FILEHANDLECLOSEREQ) {
      Wrap(object(), this);
      promise_.Reset(env->isolate(), promise);
      ref_.Reset(env->isolate(), ref);
    }
    ~CloseReq() {
      uv_fs_req_cleanup(req());
      promise_.Reset();
      ref_.Reset();
    }

    FileHandle* file_handle();

    size_t self_size() const override { return sizeof(*this); }

    void Resolve();

    void Reject(Local<Value> reason);

   private:
    Persistent<Promise> promise_;
    Persistent<Value> ref_;
  };

  // Asynchronous close
  inline MaybeLocal<Promise> ClosePromise();

  int fd_;
  bool closing_ = false;
  bool closed_ = false;
  int64_t read_offset_ = -1;
  int64_t read_length_ = -1;

  bool reading_ = false;
  std::unique_ptr<FileHandleReadWrap> current_read_ = nullptr;


  DISALLOW_COPY_AND_ASSIGN(FileHandle);
};

}  // namespace fs

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_FILE_H_

#ifndef SRC_NODE_MESSAGING_H_
#define SRC_NODE_MESSAGING_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "env.h"
#include "node_mutex.h"
#include <atomic>

namespace node {
namespace worker {

class MessagePortData;
class MessagePort;

typedef MaybeStackBuffer<v8::Local<v8::Value>, 8> TransferList;

// Single-producer, single-consumer lock-free queue, implemented as a
// linked list.
template <typename T>
class AtomicQueue {
 private:
  struct Node {
    std::atomic<Node*> next { nullptr };
    T item;
  };

 public:
  AtomicQueue() = default;
  ~AtomicQueue();

  // Push() and Pop() calls may occur concurrently, but not two Push() calls or
  // two Pop() calls.
  inline void Push(T&& item);
  inline bool Pop(T* item);
  template <typename Pred>
  inline bool PopIf(T* item, Pred&& pred);

  // Iterate over all items in the queue. This may not occur concurrently
  // with Pop() calls.
  class const_iterator {
   public:
    inline bool operator==(const const_iterator& other) const;
    inline bool operator!=(const const_iterator& other) const;
    inline const_iterator& operator++();
    inline const T& operator*() const;

   private:
    explicit const_iterator(Node* n);
    Node* n;

    friend class AtomicQueue;
  };
  inline const_iterator begin() const;
  inline const_iterator end() const;
  inline bool empty() const;
  inline size_t size() const;

  AtomicQueue(const AtomicQueue&) = delete;
  AtomicQueue& operator=(const AtomicQueue&) = delete;
  AtomicQueue(AtomicQueue&&) = delete;
  AtomicQueue& operator=(AtomicQueue&&) = delete;

 private:
  std::atomic<Node*> write_head_ { nullptr };
  std::atomic<Node*> read_head_ { nullptr };
  std::atomic<size_t> size_ { 0 };
};

template <typename T>
AtomicQueue<T>::const_iterator::const_iterator(Node* n) : n(n) {}

template <typename T>
bool AtomicQueue<T>::const_iterator::operator==(
    const const_iterator& other) const {
  return other.n == n;
}

template <typename T>
bool AtomicQueue<T>::const_iterator::operator!=(
    const const_iterator& other) const {
  return other.n != n;
}

template <typename T>
typename AtomicQueue<T>::const_iterator&
AtomicQueue<T>::const_iterator::operator++() {
  n = n->next.load();
  return *this;
}

template <typename T>
const T& AtomicQueue<T>::const_iterator::operator*() const {
  return n->item;
}

template <typename T>
typename AtomicQueue<T>::const_iterator AtomicQueue<T>::begin() const {
  return const_iterator { read_head_.load() };
}

template <typename T>
typename AtomicQueue<T>::const_iterator AtomicQueue<T>::end() const {
  return const_iterator { nullptr };
}

template <typename T>
bool AtomicQueue<T>::empty() const {
  return size() == 0;
}

template <typename T>
size_t AtomicQueue<T>::size() const {
  return size_.load();
}

template <typename T>
AtomicQueue<T>::~AtomicQueue() {
  while (Pop(nullptr));
}

template <typename T>
void AtomicQueue<T>::Push(T&& item) {
  size_.fetch_add(1);
  Node* new_head = new Node { { nullptr }, std::move(item) };
  Node* old_head = write_head_.load();
  if (old_head != nullptr)
    old_head->next.store(new_head);

  Node* null_ptr = nullptr;
  read_head_.compare_exchange_strong(null_ptr, new_head);
  write_head_.store(new_head);
}

template <typename T>
template <typename Pred>
bool AtomicQueue<T>::PopIf(T* item, Pred&& pred) {
  Node* old_head = read_head_.exchange(nullptr);
  if (old_head == nullptr || !pred(old_head->item)) {
    read_head_.exchange(old_head);
    return false;
  }
  Node* next = old_head->next.load();
  Node* null_ptr = nullptr;
  read_head_.compare_exchange_strong(null_ptr, next);
  if (next == nullptr) {
    Node* single_entry = old_head;
    write_head_.compare_exchange_strong(single_entry, nullptr);
  }
  if (item != nullptr)
    *item = std::move(old_head->item);
  delete old_head;
  size_.fetch_sub(1);
  return true;
}

template <typename T>
bool AtomicQueue<T>::Pop(T* item) {
  return PopIf(item, [](const T&) { return true; });
}

// Represents a single communication message.
class Message : public MemoryRetainer {
 public:
  // Create a Message with a specific underlying payload, in the format of the
  // V8 ValueSerializer API. If `payload` is empty, this message indicates
  // that the receiving message port should close itself.
  explicit Message(MallocedBuffer<char>&& payload = MallocedBuffer<char>());

  Message(Message&& other) = default;
  Message& operator=(Message&& other) = default;
  Message& operator=(const Message&) = delete;
  Message(const Message&) = delete;

  // Whether this is a message indicating that the port is to be closed.
  // This is the last message to be received by a MessagePort.
  bool IsCloseMessage() const;

  // Deserialize the contained JS value. May only be called once, and only
  // after Serialize() has been called (e.g. by another thread).
  v8::MaybeLocal<v8::Value> Deserialize(Environment* env,
                                        v8::Local<v8::Context> context);

  // Serialize a JS value, and optionally transfer objects, into this message.
  // The Message object retains ownership of all transferred objects until
  // deserialization.
  // The source_port parameter, if provided, will make Serialize() throw a
  // "DataCloneError" DOMException if source_port is found in transfer_list.
  v8::Maybe<bool> Serialize(Environment* env,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Value> input,
                            const TransferList& transfer_list,
                            v8::Local<v8::Object> source_port =
                                v8::Local<v8::Object>());

  // Internal method of Message that is called when a new SharedArrayBuffer
  // object is encountered in the incoming value's structure.
  void AddSharedArrayBuffer(std::shared_ptr<v8::BackingStore> backing_store);
  // Internal method of Message that is called once serialization finishes
  // and that transfers ownership of `data` to this message.
  void AddMessagePort(std::unique_ptr<MessagePortData>&& data);
  // Internal method of Message that is called when a new WebAssembly.Module
  // object is encountered in the incoming value's structure.
  uint32_t AddWASMModule(v8::CompiledWasmModule&& mod);

  // The MessagePorts that will be transferred, as recorded by Serialize().
  // Used for warning user about posting the target MessagePort to itself,
  // which will as a side effect destroy the communication channel.
  const std::vector<std::unique_ptr<MessagePortData>>& message_ports() const {
    return message_ports_;
  }

  void MemoryInfo(MemoryTracker* tracker) const override;

  SET_MEMORY_INFO_NAME(Message)
  SET_SELF_SIZE(Message)

 private:
  MallocedBuffer<char> main_message_buf_;
  std::vector<std::shared_ptr<v8::BackingStore>> array_buffers_;
  std::vector<std::shared_ptr<v8::BackingStore>> shared_array_buffers_;
  std::vector<std::unique_ptr<MessagePortData>> message_ports_;
  std::vector<v8::CompiledWasmModule> wasm_modules_;

  friend class MessagePort;
};

// This contains all data for a `MessagePort` instance that is not tied to
// a specific Environment/Isolate/event loop, for easier transfer between those.
class MessagePortData : public MemoryRetainer {
 public:
  explicit MessagePortData(MessagePort* owner);
  ~MessagePortData() override;

  MessagePortData(MessagePortData&& other) = delete;
  MessagePortData& operator=(MessagePortData&& other) = delete;
  MessagePortData(const MessagePortData& other) = delete;
  MessagePortData& operator=(const MessagePortData& other) = delete;

  // Add a message to the incoming queue and notify the receiver.
  // This may be called from any thread.
  void AddToIncomingQueue(Message&& message);

  // Turns `a` and `b` into siblings, i.e. connects the sending side of one
  // to the receiving side of the other. This is not thread-safe.
  static void Entangle(MessagePortData* a, MessagePortData* b);

  // Removes any possible sibling. This is thread-safe (it acquires both
  // `sibling_mutex_` and `mutex_`), and has to be because it is called once
  // the corresponding JS handle handle wants to close
  // which can happen on either side of a worker.
  void Disentangle();

  void MemoryInfo(MemoryTracker* tracker) const override;

  SET_MEMORY_INFO_NAME(MessagePortData)
  SET_SELF_SIZE(MessagePortData)

 private:
  AtomicQueue<Message> incoming_messages_;
  std::atomic<MessagePort*> owner_ { nullptr };
  // This mutex protects the sibling_ field and is shared between two entangled
  // MessagePorts. If both mutexes are acquired, this one needs to be
  // acquired first.
  std::shared_ptr<Mutex> sibling_mutex_ = std::make_shared<Mutex>();
  MessagePortData* sibling_ = nullptr;

  friend class MessagePort;
};

// A message port that receives messages from other threads, including
// the uv_async_t handle that is used to notify the current event loop of
// new incoming messages.
class MessagePort : public HandleWrap {
 private:
  // Create a new MessagePort. The `context` argument specifies the Context
  // instance that is used for creating the values emitted from this port.
  // This is called by MessagePort::New(), which is the public API used for
  // creating MessagePort instances.
  MessagePort(Environment* env,
              v8::Local<v8::Context> context,
              v8::Local<v8::Object> wrap);

 public:
  ~MessagePort() override;

  // Create a new message port instance, optionally over an existing
  // `MessagePortData` object.
  static MessagePort* New(Environment* env,
                          v8::Local<v8::Context> context,
                          std::unique_ptr<MessagePortData> data = nullptr);

  // Send a message, i.e. deliver it into the sibling's incoming queue.
  // If this port is closed, or if there is no sibling, this message is
  // serialized with transfers, then silently discarded.
  v8::Maybe<bool> PostMessage(Environment* env,
                              v8::Local<v8::Value> message,
                              const TransferList& transfer);

  // Start processing messages on this port as a receiving end.
  void Start();
  // Stop processing messages on this port as a receiving end.
  void Stop();

  /* constructor */
  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  /* prototype methods */
  static void PostMessage(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Start(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Stop(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Drain(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ReceiveMessage(const v8::FunctionCallbackInfo<v8::Value>& args);

  /* static */
  static void MoveToContext(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Turns `a` and `b` into siblings, i.e. connects the sending side of one
  // to the receiving side of the other. This is not thread-safe.
  static void Entangle(MessagePort* a, MessagePort* b);
  static void Entangle(MessagePort* a, MessagePortData* b);

  // Detach this port's data for transferring. After this, the MessagePortData
  // is no longer associated with this handle, although it can still receive
  // messages.
  std::unique_ptr<MessagePortData> Detach();

  void Close(
      v8::Local<v8::Value> close_callback = v8::Local<v8::Value>()) override;

  // Returns true if either data_ has been freed, or if the handle is being
  // closed. Equivalent to the [[Detached]] internal slot in the HTML Standard.
  //
  // If checking if a JavaScript MessagePort object is detached, this method
  // alone is often not enough, since the backing C++ MessagePort object may
  // have been deleted already. For all intents and purposes, an object with a
  // NULL pointer to the C++ MessagePort object is also detached.
  inline bool IsDetached() const;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(MessagePort)
  SET_SELF_SIZE(MessagePort)

 private:
  void OnClose() override;
  void OnMessage();
  void TriggerAsync();
  v8::MaybeLocal<v8::Value> ReceiveMessage(v8::Local<v8::Context> context,
                                           bool only_if_receiving);

  std::unique_ptr<MessagePortData> data_ = nullptr;
  bool receiving_messages_ = false;
  uv_async_t async_;
  v8::Global<v8::Function> emit_message_fn_;

  friend class MessagePortData;
};

v8::Local<v8::FunctionTemplate> GetMessagePortConstructorTemplate(
    Environment* env);

}  // namespace worker
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS


#endif  // SRC_NODE_MESSAGING_H_

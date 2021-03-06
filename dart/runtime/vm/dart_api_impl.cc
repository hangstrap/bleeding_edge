// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "include/dart_api.h"
#include "include/dart_mirrors_api.h"
#include "include/dart_native_api.h"

#include "platform/assert.h"
#include "vm/bigint_operations.h"
#include "vm/class_finalizer.h"
#include "vm/compiler.h"
#include "vm/dart.h"
#include "vm/dart_api_impl.h"
#include "vm/dart_api_message.h"
#include "vm/dart_api_state.h"
#include "vm/dart_entry.h"
#include "vm/debuginfo.h"
#include "vm/exceptions.h"
#include "vm/flags.h"
#include "vm/growable_array.h"
#include "vm/message.h"
#include "vm/message_handler.h"
#include "vm/native_entry.h"
#include "vm/object.h"
#include "vm/object_store.h"
#include "vm/port.h"
#include "vm/resolver.h"
#include "vm/reusable_handles.h"
#include "vm/stack_frame.h"
#include "vm/symbols.h"
#include "vm/timer.h"
#include "vm/unicode.h"
#include "vm/verifier.h"
#include "vm/version.h"

namespace dart {

DECLARE_FLAG(bool, print_class_table);
DECLARE_FLAG(bool, verify_handles);
DEFINE_FLAG(bool, trace_api, false,
            "Trace invocation of API calls (debug mode only)");

ThreadLocalKey Api::api_native_key_ = Thread::kUnsetThreadLocalKey;
Dart_Handle Api::true_handle_ = NULL;
Dart_Handle Api::false_handle_ = NULL;
Dart_Handle Api::null_handle_ = NULL;


const char* CanonicalFunction(const char* func) {
  if (strncmp(func, "dart::", 6) == 0) {
    return func + 6;
  } else {
    return func;
  }
}


static RawInstance* GetListInstance(Isolate* isolate, const Object& obj) {
  if (obj.IsInstance()) {
    const Library& core_lib = Library::Handle(Library::CoreLibrary());
    const Class& list_class =
        Class::Handle(core_lib.LookupClass(Symbols::List()));
    ASSERT(!list_class.IsNull());
    const Instance& instance = Instance::Cast(obj);
    const Class& obj_class = Class::Handle(isolate, obj.clazz());
    Error& malformed_type_error = Error::Handle(isolate);
    if (obj_class.IsSubtypeOf(TypeArguments::Handle(isolate),
                              list_class,
                              TypeArguments::Handle(isolate),
                              &malformed_type_error)) {
      ASSERT(malformed_type_error.IsNull());  // Type is a raw List.
      return instance.raw();
    }
  }
  return Instance::null();
}


Dart_Handle Api::InitNewHandle(Isolate* isolate, RawObject* raw) {
  LocalHandles* local_handles = Api::TopScope(isolate)->local_handles();
  ASSERT(local_handles != NULL);
  LocalHandle* ref = local_handles->AllocateHandle();
  ref->set_raw(raw);
  return reinterpret_cast<Dart_Handle>(ref);
}


Dart_Handle Api::NewHandle(Isolate* isolate, RawObject* raw) {
  if (raw == Object::null()) {
    return Null();
  }
  if (raw == Bool::True().raw()) {
    return True();
  }
  if (raw == Bool::False().raw()) {
    return False();
  }
  return InitNewHandle(isolate, raw);
}


RawObject* Api::UnwrapHandle(Dart_Handle object) {
#if defined(DEBUG)
  Isolate* isolate = Isolate::Current();
  ASSERT(isolate != NULL);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  ASSERT(!FLAG_verify_handles ||
         state->IsValidLocalHandle(object) ||
         Dart::vm_isolate()->api_state()->IsValidLocalHandle(object));
  ASSERT(FinalizablePersistentHandle::raw_offset() == 0 &&
         PersistentHandle::raw_offset() == 0 &&
         LocalHandle::raw_offset() == 0);
#endif
  return (reinterpret_cast<LocalHandle*>(object))->raw();
}


#define DEFINE_UNWRAP(type)                                                    \
  const type& Api::Unwrap##type##Handle(Isolate* iso,                          \
                                        Dart_Handle dart_handle) {             \
    const Object& obj = Object::Handle(iso, Api::UnwrapHandle(dart_handle));   \
    if (obj.Is##type()) {                                                      \
      return type::Cast(obj);                                                  \
    }                                                                          \
    return type::Handle(iso);                                                  \
  }
CLASS_LIST_FOR_HANDLES(DEFINE_UNWRAP)
#undef DEFINE_UNWRAP


PersistentHandle* Api::UnwrapAsPersistentHandle(Dart_PersistentHandle object) {
  ASSERT(Isolate::Current()->api_state()->IsValidPersistentHandle(object));
  return reinterpret_cast<PersistentHandle*>(object);
}


FinalizablePersistentHandle* Api::UnwrapAsWeakPersistentHandle(
    Dart_WeakPersistentHandle object) {
  ASSERT(Isolate::Current()->api_state()->IsValidWeakPersistentHandle(object) ||
         Isolate::Current()->api_state()->
             IsValidPrologueWeakPersistentHandle(object));
  return reinterpret_cast<FinalizablePersistentHandle*>(object);
}


FinalizablePersistentHandle* Api::UnwrapAsPrologueWeakPersistentHandle(
    Dart_WeakPersistentHandle object) {
  ASSERT(Isolate::Current()->api_state()->IsValidPrologueWeakPersistentHandle(
      object));
  return reinterpret_cast<FinalizablePersistentHandle*>(object);
}


Dart_Handle Api::CheckIsolateState(Isolate* isolate) {
  if (!isolate->AllowClassFinalization()) {
    // Class finalization is blocked for the isolate. Do nothing.
    return Api::Success();
  }
  if (ClassFinalizer::FinalizePendingClasses()) {
    return Api::Success();
  }
  ASSERT(isolate->object_store()->sticky_error() != Object::null());
  return Api::NewHandle(isolate, isolate->object_store()->sticky_error());
}


Dart_Isolate Api::CastIsolate(Isolate* isolate) {
  return reinterpret_cast<Dart_Isolate>(isolate);
}


Dart_Handle Api::NewError(const char* format, ...) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);

  va_list args;
  va_start(args, format);
  intptr_t len = OS::VSNPrint(NULL, 0, format, args);
  va_end(args);

  char* buffer = isolate->current_zone()->Alloc<char>(len + 1);
  va_list args2;
  va_start(args2, format);
  OS::VSNPrint(buffer, (len + 1), format, args2);
  va_end(args2);

  const String& message = String::Handle(isolate, String::New(buffer));
  return Api::NewHandle(isolate, ApiError::New(message));
}


void Api::SetupAcquiredError(Isolate* isolate) {
  ASSERT(isolate != NULL);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  state->SetupAcquiredError();
}


Dart_Handle Api::AcquiredError(Isolate* isolate) {
  ASSERT(isolate != NULL);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  PersistentHandle* acquired_error_handle = state->AcquiredError();
  return reinterpret_cast<Dart_Handle>(acquired_error_handle);
}


Dart_Handle Api::Null() {
  return null_handle_;
}


Dart_Handle Api::True() {
  return true_handle_;
}


Dart_Handle Api::False() {
  return false_handle_;
}


ApiLocalScope* Api::TopScope(Isolate* isolate) {
  ASSERT(isolate != NULL);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  ApiLocalScope* scope = state->top_scope();
  ASSERT(scope != NULL);
  return scope;
}


void Api::InitOnce() {
  ASSERT(api_native_key_ == Thread::kUnsetThreadLocalKey);
  api_native_key_ = Thread::CreateThreadLocal();
  ASSERT(api_native_key_ != Thread::kUnsetThreadLocalKey);
}


void Api::InitHandles() {
  Isolate* isolate = Isolate::Current();
  ASSERT(isolate != NULL);
  ASSERT(isolate == Dart::vm_isolate());
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  ASSERT(true_handle_ == NULL);
  true_handle_ = Api::InitNewHandle(isolate, Bool::True().raw());

  ASSERT(false_handle_ == NULL);
  false_handle_ = Api::InitNewHandle(isolate, Bool::False().raw());

  ASSERT(null_handle_ == NULL);
  null_handle_ = Api::InitNewHandle(isolate, Object::null());
}


bool Api::StringGetPeerHelper(Dart_NativeArguments args,
                              int arg_index,
                              void** peer) {
  NoGCScope no_gc_scope;
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  RawObject* raw_obj = arguments->NativeArgAt(arg_index);
  intptr_t cid = raw_obj->GetClassId();
  if (cid == kExternalOneByteStringCid) {
    RawExternalOneByteString* raw_string =
        reinterpret_cast<RawExternalOneByteString*>(raw_obj)->ptr();
    ExternalStringData<uint8_t>* data = raw_string->external_data_;
    *peer = data->peer();
    return true;
  }
  if (cid == kOneByteStringCid || cid == kTwoByteStringCid) {
    Isolate* isolate = arguments->isolate();
    *peer = isolate->heap()->GetPeer(raw_obj);
    return (*peer != 0);
  }
  if (cid == kExternalTwoByteStringCid) {
    RawExternalTwoByteString* raw_string =
        reinterpret_cast<RawExternalTwoByteString*>(raw_obj)->ptr();
    ExternalStringData<uint16_t>* data = raw_string->external_data_;
    *peer = data->peer();
    return true;
  }
  return false;
}


void Api::SetWeakHandleReturnValue(NativeArguments* args,
                                   Dart_WeakPersistentHandle retval) {
  args->SetReturnUnsafe(Api::UnwrapAsWeakPersistentHandle(retval)->raw());
}


// --- Handles ---

DART_EXPORT bool Dart_IsError(Dart_Handle handle) {
  TRACE_API_CALL(CURRENT_FUNC);
  return RawObject::IsErrorClassId(Api::ClassId(handle));
}


DART_EXPORT bool Dart_IsApiError(Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  return Api::ClassId(object) == kApiErrorCid;
}


DART_EXPORT bool Dart_IsUnhandledExceptionError(Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  return Api::ClassId(object) == kUnhandledExceptionCid;
}


DART_EXPORT bool Dart_IsCompilationError(Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  return Api::ClassId(object) == kLanguageErrorCid;
}


DART_EXPORT bool Dart_IsFatalError(Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  return Api::ClassId(object) == kUnwindErrorCid;
}


DART_EXPORT const char* Dart_GetError(Dart_Handle handle) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(handle));
  if (obj.IsError()) {
    const Error& error = Error::Cast(obj);
    const char* str = error.ToErrorCString();
    intptr_t len = strlen(str) + 1;
    char* str_copy = Api::TopScope(isolate)->zone()->Alloc<char>(len);
    strncpy(str_copy, str, len);
    // Strip a possible trailing '\n'.
    if ((len > 1) && (str_copy[len - 2] == '\n')) {
      str_copy[len - 2] = '\0';
    }
    return str_copy;
  } else {
    return "";
  }
}


DART_EXPORT bool Dart_ErrorHasException(Dart_Handle handle) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(handle));
  return obj.IsUnhandledException();
}


DART_EXPORT Dart_Handle Dart_ErrorGetException(Dart_Handle handle) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(handle));
  if (obj.IsUnhandledException()) {
    const UnhandledException& error = UnhandledException::Cast(obj);
    return Api::NewHandle(isolate, error.exception());
  } else if (obj.IsError()) {
    return Api::NewError("This error is not an unhandled exception error.");
  } else {
    return Api::NewError("Can only get exceptions from error handles.");
  }
}


DART_EXPORT Dart_Handle Dart_ErrorGetStacktrace(Dart_Handle handle) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(handle));
  if (obj.IsUnhandledException()) {
    const UnhandledException& error = UnhandledException::Cast(obj);
    return Api::NewHandle(isolate, error.stacktrace());
  } else if (obj.IsError()) {
    return Api::NewError("This error is not an unhandled exception error.");
  } else {
    return Api::NewError("Can only get stacktraces from error handles.");
  }
}


// Deprecated.
// TODO(turnidge): Remove all uses and delete.
DART_EXPORT Dart_Handle Dart_Error(const char* format, ...) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);

  va_list args;
  va_start(args, format);
  intptr_t len = OS::VSNPrint(NULL, 0, format, args);
  va_end(args);

  char* buffer = isolate->current_zone()->Alloc<char>(len + 1);
  va_list args2;
  va_start(args2, format);
  OS::VSNPrint(buffer, (len + 1), format, args2);
  va_end(args2);

  const String& message = String::Handle(isolate, String::New(buffer));
  return Api::NewHandle(isolate, ApiError::New(message));
}


// TODO(turnidge): This clones Api::NewError.  I need to use va_copy to
// fix this but not sure if it available on all of our builds.
DART_EXPORT Dart_Handle Dart_NewApiError(const char* format, ...) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);

  va_list args;
  va_start(args, format);
  intptr_t len = OS::VSNPrint(NULL, 0, format, args);
  va_end(args);

  char* buffer = isolate->current_zone()->Alloc<char>(len + 1);
  va_list args2;
  va_start(args2, format);
  OS::VSNPrint(buffer, (len + 1), format, args2);
  va_end(args2);

  const String& message = String::Handle(isolate, String::New(buffer));
  return Api::NewHandle(isolate, ApiError::New(message));
}


DART_EXPORT Dart_Handle Dart_NewUnhandledExceptionError(Dart_Handle exception) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);

  const Instance& obj = Api::UnwrapInstanceHandle(isolate, exception);
  if (obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, exception, Instance);
  }
  const Instance& stacktrace = Instance::Handle(isolate);
  return Api::NewHandle(isolate, UnhandledException::New(obj, stacktrace));
}


DART_EXPORT Dart_Handle Dart_PropagateError(Dart_Handle handle) {
  Isolate* isolate = Isolate::Current();
  {
    const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(handle));
    if (!obj.IsError()) {
      return Api::NewError(
          "%s expects argument 'handle' to be an error handle.  "
          "Did you forget to check Dart_IsError first?",
          CURRENT_FUNC);
    }
  }
  if (isolate->top_exit_frame_info() == 0) {
    // There are no dart frames on the stack so it would be illegal to
    // propagate an error here.
    return Api::NewError("No Dart frames on stack, cannot propagate error.");
  }

  // Unwind all the API scopes till the exit frame before propagating.
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  const Error* error;
  {
    // We need to preserve the error object across the destruction of zones
    // when the ApiScopes are unwound.  By using NoGCScope, we can ensure
    // that GC won't touch the raw error object before creating a valid
    // handle for it in the surviving zone.
    NoGCScope no_gc;
    RawError* raw_error = Api::UnwrapErrorHandle(isolate, handle).raw();
    state->UnwindScopes(isolate->top_exit_frame_info());
    error = &Error::Handle(isolate, raw_error);
  }
  Exceptions::PropagateError(*error);
  UNREACHABLE();
  return Api::NewError("Cannot reach here.  Internal error.");
}


DART_EXPORT void _Dart_ReportErrorHandle(const char* file,
                                         int line,
                                         const char* handle,
                                         const char* message) {
  fprintf(stderr, "%s:%d: error handle: '%s':\n    '%s'\n",
          file, line, handle, message);
  OS::Abort();
}


DART_EXPORT Dart_Handle Dart_ToString(Dart_Handle object) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(object));
  if (obj.IsString()) {
    return Api::NewHandle(isolate, obj.raw());
  } else if (obj.IsInstance()) {
    CHECK_CALLBACK_STATE(isolate);
    const Instance& receiver = Instance::Cast(obj);
    return Api::NewHandle(isolate, DartLibraryCalls::ToString(receiver));
  } else {
    CHECK_CALLBACK_STATE(isolate);
    // This is a VM internal object. Call the C++ method of printing.
    return Api::NewHandle(isolate, String::New(obj.ToCString()));
  }
}


DART_EXPORT bool Dart_IdentityEquals(Dart_Handle obj1, Dart_Handle obj2) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  {
    NoGCScope ngc;
    if (Api::UnwrapHandle(obj1) == Api::UnwrapHandle(obj2)) {
      return true;
    }
  }
  const Object& object1 = Object::Handle(isolate, Api::UnwrapHandle(obj1));
  const Object& object2 = Object::Handle(isolate, Api::UnwrapHandle(obj2));
  if (object1.IsInstance() && object2.IsInstance()) {
    return Instance::Cast(object1).IsIdenticalTo(Instance::Cast(object2));
  }
  return false;
}


DART_EXPORT Dart_Handle Dart_HandleFromPersistent(
    Dart_PersistentHandle object) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  ASSERT(state->IsValidPersistentHandle(object));
  return Api::NewHandle(isolate,
                        reinterpret_cast<PersistentHandle*>(object)->raw());
}


DART_EXPORT Dart_Handle Dart_HandleFromWeakPersistent(
    Dart_WeakPersistentHandle object) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  ASSERT(state->IsValidWeakPersistentHandle(object) ||
         state->IsValidPrologueWeakPersistentHandle(object));
  return Api::NewHandle(
      isolate, reinterpret_cast<FinalizablePersistentHandle*>(object)->raw());
}


DART_EXPORT Dart_PersistentHandle Dart_NewPersistentHandle(Dart_Handle object) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  const Object& old_ref = Object::Handle(isolate, Api::UnwrapHandle(object));
  PersistentHandle* new_ref = state->persistent_handles().AllocateHandle();
  new_ref->set_raw(old_ref);
  return reinterpret_cast<Dart_PersistentHandle>(new_ref);
}

static Dart_WeakPersistentHandle AllocateFinalizableHandle(
    Isolate* isolate,
    FinalizablePersistentHandles* handles,
    Dart_Handle object,
    void* peer,
    Dart_WeakPersistentHandleFinalizer callback) {
  const Object& ref = Object::Handle(isolate, Api::UnwrapHandle(object));
  FinalizablePersistentHandle* finalizable_ref = handles->AllocateHandle();
  finalizable_ref->set_raw(ref);
  finalizable_ref->set_peer(peer);
  finalizable_ref->set_callback(callback);
  return reinterpret_cast<Dart_WeakPersistentHandle>(finalizable_ref);
}


DART_EXPORT Dart_WeakPersistentHandle Dart_NewWeakPersistentHandle(
    Dart_Handle object,
    void* peer,
    Dart_WeakPersistentHandleFinalizer callback) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  return AllocateFinalizableHandle(isolate,
                                   &state->weak_persistent_handles(),
                                   object,
                                   peer,
                                   callback);
}


DART_EXPORT Dart_WeakPersistentHandle Dart_NewPrologueWeakPersistentHandle(
    Dart_Handle object,
    void* peer,
    Dart_WeakPersistentHandleFinalizer callback) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  return AllocateFinalizableHandle(isolate,
                                   &state->prologue_weak_persistent_handles(),
                                   object,
                                   peer,
                                   callback);
}


DART_EXPORT void Dart_DeletePersistentHandle(Dart_PersistentHandle object) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  PersistentHandle* ref = Api::UnwrapAsPersistentHandle(object);
  ASSERT(!state->IsProtectedHandle(ref));
  if (!state->IsProtectedHandle(ref)) {
    state->persistent_handles().FreeHandle(ref);
  }
}


DART_EXPORT void Dart_DeleteWeakPersistentHandle(
    Dart_WeakPersistentHandle object) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  if (state->IsValidPrologueWeakPersistentHandle(object)) {
    FinalizablePersistentHandle* prologue_weak_ref =
        Api::UnwrapAsPrologueWeakPersistentHandle(object);
    state->prologue_weak_persistent_handles().FreeHandle(prologue_weak_ref);
    return;
  }
  FinalizablePersistentHandle* weak_ref =
      Api::UnwrapAsWeakPersistentHandle(object);
  state->weak_persistent_handles().FreeHandle(weak_ref);
  return;
}


DART_EXPORT bool Dart_IsPrologueWeakPersistentHandle(
    Dart_WeakPersistentHandle object) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  return state->IsValidPrologueWeakPersistentHandle(object);
}


DART_EXPORT Dart_Handle Dart_NewWeakReferenceSet(
    Dart_WeakPersistentHandle* keys,
    intptr_t num_keys,
    Dart_WeakPersistentHandle* values,
    intptr_t num_values) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  if (keys == NULL) {
    RETURN_NULL_ERROR(keys);
  }
  if (num_keys <= 0) {
    return Api::NewError(
        "%s expects argument 'num_keys' to be greater than 0.",
        CURRENT_FUNC);
  }
  if (values == NULL) {
    RETURN_NULL_ERROR(values);
  }
  if (num_values <= 0) {
    return Api::NewError(
        "%s expects argument 'num_values' to be greater than 0.",
        CURRENT_FUNC);
  }

  WeakReferenceSet* reference_set = new WeakReferenceSet(keys, num_keys,
                                                         values, num_values);
  state->DelayWeakReferenceSet(reference_set);
  return Api::Success();
}


// --- Garbage Collection Callbacks --

DART_EXPORT Dart_Handle Dart_AddGcPrologueCallback(
    Dart_GcPrologueCallback callback) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  GcPrologueCallbacks& callbacks = isolate->gc_prologue_callbacks();
  if (callbacks.Contains(callback)) {
    return Api::NewError(
        "%s permits only one instance of 'callback' to be present in the "
        "prologue callback list.",
        CURRENT_FUNC);
  }
  callbacks.Add(callback);
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_RemoveGcPrologueCallback(
    Dart_GcPrologueCallback callback) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  GcPrologueCallbacks& callbacks = isolate->gc_prologue_callbacks();
  if (!callbacks.Contains(callback)) {
    return Api::NewError(
        "%s expects 'callback' to be present in the prologue callback list.",
        CURRENT_FUNC);
  }
  callbacks.Remove(callback);
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_AddGcEpilogueCallback(
    Dart_GcEpilogueCallback callback) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  GcEpilogueCallbacks& callbacks = isolate->gc_epilogue_callbacks();
  if (callbacks.Contains(callback)) {
    return Api::NewError(
        "%s permits only one instance of 'callback' to be present in the "
        "epilogue callback list.",
        CURRENT_FUNC);
  }
  callbacks.Add(callback);
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_RemoveGcEpilogueCallback(
    Dart_GcEpilogueCallback callback) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  GcEpilogueCallbacks& callbacks = isolate->gc_epilogue_callbacks();
  if (!callbacks.Contains(callback)) {
    return Api::NewError(
        "%s expects 'callback' to be present in the epilogue callback list.",
        CURRENT_FUNC);
  }
  callbacks.Remove(callback);
  return Api::Success();
}


// --- Initialization and Globals ---

DART_EXPORT const char* Dart_VersionString() {
  return Version::String();
}

DART_EXPORT bool Dart_Initialize(
    Dart_IsolateCreateCallback create,
    Dart_IsolateInterruptCallback interrupt,
    Dart_IsolateUnhandledExceptionCallback unhandled,
    Dart_IsolateShutdownCallback shutdown,
    Dart_FileOpenCallback file_open,
    Dart_FileReadCallback file_read,
    Dart_FileWriteCallback file_write,
    Dart_FileCloseCallback file_close) {
  const char* err_msg = Dart::InitOnce(create, interrupt, unhandled, shutdown,
                                       file_open, file_read, file_write,
                                       file_close);
  if (err_msg != NULL) {
    OS::PrintErr("Dart_Initialize: %s\n", err_msg);
    return false;
  }
  return true;
}


DART_EXPORT bool Dart_Cleanup() {
  CHECK_NO_ISOLATE(Isolate::Current());
  const char* err_msg = Dart::Cleanup();
  if (err_msg != NULL) {
    OS::PrintErr("Dart_Cleanup: %s\n", err_msg);
    return false;
  }
  return true;
}


DART_EXPORT bool Dart_SetVMFlags(int argc, const char** argv) {
  return Flags::ProcessCommandLineFlags(argc, argv);
}


DART_EXPORT bool Dart_IsVMFlagSet(const char* flag_name) {
  if (Flags::Lookup(flag_name) != NULL) {
    return true;
  }
  return false;
}


// --- Isolates ---

static char* BuildIsolateName(const char* script_uri,
                              const char* main) {
  if (script_uri == NULL) {
    // Just use the main as the name.
    if (main == NULL) {
      return strdup("isolate");
    } else {
      return strdup(main);
    }
  }

  // Skip past any slashes and backslashes in the script uri.
  const char* last_slash = strrchr(script_uri, '/');
  if (last_slash != NULL) {
    script_uri = last_slash + 1;
  }
  const char* last_backslash = strrchr(script_uri, '\\');
  if (last_backslash != NULL) {
    script_uri = last_backslash + 1;
  }
  if (main == NULL) {
    main = "main";
  }

  char* chars = NULL;
  intptr_t len = OS::SNPrint(NULL, 0, "%s$%s", script_uri, main) + 1;
  chars = reinterpret_cast<char*>(malloc(len));
  OS::SNPrint(chars, len, "%s$%s", script_uri, main);
  return chars;
}


DART_EXPORT Dart_Isolate Dart_CreateIsolate(const char* script_uri,
                                            const char* main,
                                            const uint8_t* snapshot,
                                            void* callback_data,
                                            char** error) {
  TRACE_API_CALL(CURRENT_FUNC);
  char* isolate_name = BuildIsolateName(script_uri, main);
  Isolate* isolate = Dart::CreateIsolate(isolate_name);
  free(isolate_name);
  {
    StackZone zone(isolate);
    HANDLESCOPE(isolate);
    const Error& error_obj =
        Error::Handle(isolate,
                      Dart::InitializeIsolate(snapshot, callback_data));
    if (error_obj.IsNull()) {
      START_TIMER(time_total_runtime);
      return reinterpret_cast<Dart_Isolate>(isolate);
    }
    *error = strdup(error_obj.ToErrorCString());
  }
  Dart::ShutdownIsolate();
  return reinterpret_cast<Dart_Isolate>(NULL);
}


DART_EXPORT void Dart_ShutdownIsolate() {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  {
    StackZone zone(isolate);
    HandleScope handle_scope(isolate);
    Dart::RunShutdownCallback();
  }
  STOP_TIMER(time_total_runtime);
  Dart::ShutdownIsolate();
}


DART_EXPORT Dart_Isolate Dart_CurrentIsolate() {
  return Api::CastIsolate(Isolate::Current());
}


DART_EXPORT void* Dart_CurrentIsolateData() {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  return isolate->init_callback_data();
}


DART_EXPORT Dart_Handle Dart_DebugName() {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  return Api::NewHandle(isolate, String::New(isolate->name()));
}



DART_EXPORT void Dart_EnterIsolate(Dart_Isolate dart_isolate) {
  CHECK_NO_ISOLATE(Isolate::Current());
  Isolate* isolate = reinterpret_cast<Isolate*>(dart_isolate);
  Isolate::SetCurrent(isolate);
}


DART_EXPORT void Dart_ExitIsolate() {
  CHECK_ISOLATE(Isolate::Current());
  Isolate::SetCurrent(NULL);
}


static uint8_t* ApiReallocate(uint8_t* ptr,
                              intptr_t old_size,
                              intptr_t new_size) {
  return Api::TopScope(Isolate::Current())->zone()->Realloc<uint8_t>(
      ptr, old_size, new_size);
}


DART_EXPORT Dart_Handle Dart_CreateSnapshot(uint8_t** buffer,
                                            intptr_t* size) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  TIMERSCOPE(time_creating_snapshot);
  if (buffer == NULL) {
    RETURN_NULL_ERROR(buffer);
  }
  if (size == NULL) {
    RETURN_NULL_ERROR(size);
  }
  Dart_Handle state = Api::CheckIsolateState(isolate);
  if (::Dart_IsError(state)) {
    return state;
  }
  // Since this is only a snapshot the root library should not be set.
  isolate->object_store()->set_root_library(Library::Handle(isolate));
  FullSnapshotWriter writer(buffer, ApiReallocate);
  writer.WriteFullSnapshot();
  *size = writer.BytesWritten();
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_CreateScriptSnapshot(uint8_t** buffer,
                                                  intptr_t* size) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  TIMERSCOPE(time_creating_snapshot);
  if (buffer == NULL) {
    RETURN_NULL_ERROR(buffer);
  }
  if (size == NULL) {
    RETURN_NULL_ERROR(size);
  }
  Dart_Handle state = Api::CheckIsolateState(isolate);
  if (::Dart_IsError(state)) {
    return state;
  }
  Library& library =
      Library::Handle(isolate, isolate->object_store()->root_library());
  if (library.IsNull()) {
    return
        Api::NewError("%s expects the isolate to have a script loaded in it.",
                      CURRENT_FUNC);
  }
  ScriptSnapshotWriter writer(buffer, ApiReallocate);
  writer.WriteScriptSnapshot(library);
  *size = writer.BytesWritten();
  return Api::Success();
}


DART_EXPORT void Dart_InterruptIsolate(Dart_Isolate isolate) {
  TRACE_API_CALL(CURRENT_FUNC);
  if (isolate == NULL) {
    FATAL1("%s expects argument 'isolate' to be non-null.",  CURRENT_FUNC);
  }
  Isolate* iso = reinterpret_cast<Isolate*>(isolate);
  iso->ScheduleInterrupts(Isolate::kApiInterrupt);
}


DART_EXPORT bool Dart_IsolateMakeRunnable(Dart_Isolate isolate) {
  CHECK_NO_ISOLATE(Isolate::Current());
  if (isolate == NULL) {
    FATAL1("%s expects argument 'isolate' to be non-null.",  CURRENT_FUNC);
  }
  Isolate* iso = reinterpret_cast<Isolate*>(isolate);
  return iso->MakeRunnable();
}


// --- Messages and Ports ---

DART_EXPORT void Dart_SetMessageNotifyCallback(
    Dart_MessageNotifyCallback message_notify_callback) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  isolate->set_message_notify_callback(message_notify_callback);
}


struct RunLoopData {
  Monitor* monitor;
  bool done;
};


static void RunLoopDone(uword param) {
  RunLoopData* data = reinterpret_cast<RunLoopData*>(param);
  ASSERT(data->monitor != NULL);
  MonitorLocker ml(data->monitor);
  data->done = true;
  ml.Notify();
}


DART_EXPORT Dart_Handle Dart_RunLoop() {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE_SCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);
  Monitor monitor;
  MonitorLocker ml(&monitor);
  {
    SwitchIsolateScope switch_scope(NULL);

    RunLoopData data;
    data.monitor = &monitor;
    data.done = false;
    isolate->message_handler()->Run(
        Dart::thread_pool(),
        NULL, RunLoopDone, reinterpret_cast<uword>(&data));
    while (!data.done) {
      ml.Wait();
    }
  }
  if (isolate->object_store()->sticky_error() != Object::null()) {
    Dart_Handle error = Api::NewHandle(isolate,
                                       isolate->object_store()->sticky_error());
    isolate->object_store()->clear_sticky_error();
    return error;
  }
  if (FLAG_print_class_table) {
    isolate->class_table()->Print();
  }
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_HandleMessage() {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE_SCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);
  if (!isolate->message_handler()->HandleNextMessage()) {
    Dart_Handle error = Api::NewHandle(isolate,
                                       isolate->object_store()->sticky_error());
    isolate->object_store()->clear_sticky_error();
    return error;
  }
  return Api::Success();
}


DART_EXPORT bool Dart_HasLivePorts() {
  Isolate* isolate = Isolate::Current();
  ASSERT(isolate);
  return isolate->message_handler()->HasLivePorts();
}


static uint8_t* allocator(uint8_t* ptr, intptr_t old_size, intptr_t new_size) {
  void* new_ptr = realloc(reinterpret_cast<void*>(ptr), new_size);
  return reinterpret_cast<uint8_t*>(new_ptr);
}


DART_EXPORT bool Dart_Post(Dart_Port port_id, Dart_Handle handle) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& object = Object::Handle(isolate, Api::UnwrapHandle(handle));
  uint8_t* data = NULL;
  MessageWriter writer(&data, &allocator);
  writer.WriteMessage(object);
  intptr_t len = writer.BytesWritten();
  return PortMap::PostMessage(new Message(
      port_id, Message::kIllegalPort, data, len, Message::kNormalPriority));
}


DART_EXPORT Dart_Handle Dart_NewSendPort(Dart_Port port_id) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);
  return Api::NewHandle(isolate, DartLibraryCalls::NewSendPort(port_id));
}


DART_EXPORT Dart_Handle Dart_GetReceivePort(Dart_Port port_id) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);

  Library& isolate_lib = Library::Handle(isolate, Library::IsolateLibrary());
  ASSERT(!isolate_lib.IsNull());
  const String& class_name = String::Handle(
      isolate, isolate_lib.PrivateName(Symbols::_ReceivePortImpl()));
  // TODO(asiva): Symbols should contain private keys.
  const String& function_name =
      String::Handle(isolate_lib.PrivateName(Symbols::_get_or_create()));
  const int kNumArguments = 1;
  const Function& function = Function::Handle(
      isolate,
      Resolver::ResolveStatic(isolate_lib,
                              class_name,
                              function_name,
                              kNumArguments,
                              Object::empty_array(),
                              Resolver::kIsQualified));
  ASSERT(!function.IsNull());
  const Array& args = Array::Handle(isolate, Array::New(kNumArguments));
  args.SetAt(0, Integer::Handle(isolate, Integer::New(port_id)));
  return Api::NewHandle(isolate, DartEntry::InvokeFunction(function, args));
}


DART_EXPORT Dart_Port Dart_GetMainPortId() {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  return isolate->main_port();
}


// --- Scopes ----

DART_EXPORT void Dart_EnterScope() {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  ApiLocalScope* new_scope = state->reusable_scope();
  if (new_scope == NULL) {
    new_scope = new ApiLocalScope(state->top_scope(),
                                  isolate->top_exit_frame_info());
    ASSERT(new_scope != NULL);
  } else {
    new_scope->Reinit(isolate,
                      state->top_scope(),
                      isolate->top_exit_frame_info());
    state->set_reusable_scope(NULL);
  }
  state->set_top_scope(new_scope);  // New scope is now the top scope.
}


DART_EXPORT void Dart_ExitScope() {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE_SCOPE(isolate);
  ApiState* state = isolate->api_state();
  ApiLocalScope* scope = state->top_scope();
  ApiLocalScope* reusable_scope = state->reusable_scope();
  state->set_top_scope(scope->previous());  // Reset top scope to previous.
  if (reusable_scope == NULL) {
    scope->Reset(isolate);  // Reset the old scope which we just exited.
    state->set_reusable_scope(scope);
  } else {
    ASSERT(reusable_scope != scope);
    delete scope;
  }
}


DART_EXPORT uint8_t* Dart_ScopeAllocate(intptr_t size) {
  Zone* zone;
  Isolate* isolate = Isolate::Current();
  if (isolate != NULL) {
    ApiState* state = isolate->api_state();
    if (state == NULL) return NULL;
    ApiLocalScope* scope = state->top_scope();
    zone = scope->zone();
  } else {
    ApiNativeScope* scope = ApiNativeScope::Current();
    if (scope == NULL) return NULL;
    zone = scope->zone();
  }
  return reinterpret_cast<uint8_t*>(zone->AllocUnsafe(size));
}


// --- Objects ----

DART_EXPORT Dart_Handle Dart_Null() {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE_SCOPE(isolate);
  return Api::Null();
}


DART_EXPORT bool Dart_IsNull(Dart_Handle object) {
  return Api::UnwrapHandle(object) == Object::null();
}


DART_EXPORT Dart_Handle Dart_ObjectEquals(Dart_Handle obj1, Dart_Handle obj2,
                                          bool* value) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);
  const Instance& expected =
      Instance::CheckedHandle(isolate, Api::UnwrapHandle(obj1));
  const Instance& actual =
      Instance::CheckedHandle(isolate, Api::UnwrapHandle(obj2));
  const Object& result =
      Object::Handle(isolate, DartLibraryCalls::Equals(expected, actual));
  if (result.IsBool()) {
    *value = Bool::Cast(result).value();
    return Api::Success();
  } else if (result.IsError()) {
    return Api::NewHandle(isolate, result.raw());
  } else {
    return Api::NewError("Expected boolean result from ==");
  }
}


// TODO(iposva): This call actually implements IsInstanceOfClass.
// Do we also need a real Dart_IsInstanceOf, which should take an instance
// rather than an object?
DART_EXPORT Dart_Handle Dart_ObjectIsType(Dart_Handle object,
                                          Dart_Handle type,
                                          bool* value) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);

  const Type& type_obj = Api::UnwrapTypeHandle(isolate, type);
  if (type_obj.IsNull()) {
    *value = false;
    RETURN_TYPE_ERROR(isolate, type, Type);
  }
  if (object == Api::Null()) {
    *value = false;
    return Api::Success();
  }
  const Instance& instance = Api::UnwrapInstanceHandle(isolate, object);
  if (instance.IsNull()) {
    *value = false;
    RETURN_TYPE_ERROR(isolate, object, Instance);
  }
  // Finalize all classes.
  Dart_Handle state = Api::CheckIsolateState(isolate);
  if (::Dart_IsError(state)) {
    *value = false;
    return state;
  }
  CHECK_CALLBACK_STATE(isolate);
  Error& malformed_type_error = Error::Handle(isolate);
  *value = instance.IsInstanceOf(type_obj,
                                 Object::null_abstract_type_arguments(),
                                 &malformed_type_error);
  ASSERT(malformed_type_error.IsNull());  // Type was created from a class.
  return Api::Success();
}


DART_EXPORT bool Dart_IsInstance(Dart_Handle object) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(object));
  return obj.IsInstance();
}


DART_EXPORT bool Dart_IsNumber(Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  return RawObject::IsNumberClassId(Api::ClassId(object));
}


DART_EXPORT bool Dart_IsInteger(Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  return RawObject::IsIntegerClassId(Api::ClassId(object));
}


DART_EXPORT bool Dart_IsDouble(Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  return Api::ClassId(object) == kDoubleCid;
}


DART_EXPORT bool Dart_IsBoolean(Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  return Api::ClassId(object) == kBoolCid;
}


DART_EXPORT bool Dart_IsString(Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  return RawObject::IsStringClassId(Api::ClassId(object));
}


DART_EXPORT bool Dart_IsStringLatin1(Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  return RawObject::IsOneByteStringClassId(Api::ClassId(object));
}


DART_EXPORT bool Dart_IsExternalString(Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  return RawObject::IsExternalStringClassId(Api::ClassId(object));
}


DART_EXPORT bool Dart_IsList(Dart_Handle object) {
  if (RawObject::IsBuiltinListClassId(Api::ClassId(object))) {
    TRACE_API_CALL(CURRENT_FUNC);
    return true;
  }

  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(object));
  return GetListInstance(isolate, obj) != Instance::null();
}


DART_EXPORT bool Dart_IsLibrary(Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  return Api::ClassId(object) == kLibraryCid;
}


DART_EXPORT bool Dart_IsType(Dart_Handle handle) {
  TRACE_API_CALL(CURRENT_FUNC);
  return Api::ClassId(handle) == kTypeCid;
}


DART_EXPORT bool Dart_IsFunction(Dart_Handle handle) {
  TRACE_API_CALL(CURRENT_FUNC);
  return Api::ClassId(handle) == kFunctionCid;
}


DART_EXPORT bool Dart_IsVariable(Dart_Handle handle) {
  TRACE_API_CALL(CURRENT_FUNC);
  return Api::ClassId(handle) == kFieldCid;
}


DART_EXPORT bool Dart_IsTypeVariable(Dart_Handle handle) {
  TRACE_API_CALL(CURRENT_FUNC);
  return Api::ClassId(handle) == kTypeParameterCid;
}


DART_EXPORT bool Dart_IsClosure(Dart_Handle object) {
  // We can't use a fast class index check here because there are many
  // different signature classes for closures.
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Instance& closure_obj = Api::UnwrapInstanceHandle(isolate, object);
  return (!closure_obj.IsNull() && closure_obj.IsClosure());
}


// --- Instances ----

DART_EXPORT Dart_Handle Dart_InstanceGetType(Dart_Handle instance) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(instance));
  if (obj.IsNull()) {
    return Api::NewHandle(isolate, isolate->object_store()->null_type());
  }
  if (!obj.IsInstance()) {
    RETURN_TYPE_ERROR(isolate, instance, Instance);
  }
  const Type& type = Type::Handle(Instance::Cast(obj).GetType());
  return Api::NewHandle(isolate, type.Canonicalize());
}


// --- Numbers, Integers and Doubles ----

DART_EXPORT Dart_Handle Dart_IntegerFitsIntoInt64(Dart_Handle integer,
                                                  bool* fits) {
  // Fast path for Smis and Mints.
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  intptr_t class_id = Api::ClassId(integer);
  if (class_id == kSmiCid || class_id == kMintCid) {
    *fits = true;
    return Api::Success();
  }
  // Slow path for Mints and Bigints.
  DARTSCOPE(isolate);
  const Integer& int_obj = Api::UnwrapIntegerHandle(isolate, integer);
  if (int_obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, integer, Integer);
  }
  ASSERT(!BigintOperations::FitsIntoInt64(Bigint::Cast(int_obj)));
  *fits = false;
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_IntegerFitsIntoUint64(Dart_Handle integer,
                                                   bool* fits) {
  // Fast path for Smis.
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  if (Api::IsSmi(integer)) {
    *fits = (Api::SmiValue(integer) >= 0);
    return Api::Success();
  }
  // Slow path for Mints and Bigints.
  DARTSCOPE(isolate);
  const Integer& int_obj = Api::UnwrapIntegerHandle(isolate, integer);
  if (int_obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, integer, Integer);
  }
  ASSERT(!int_obj.IsSmi());
  if (int_obj.IsMint()) {
    *fits = !int_obj.IsNegative();
  } else {
    *fits = BigintOperations::FitsIntoUint64(Bigint::Cast(int_obj));
  }
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_NewInteger(int64_t value) {
  // Fast path for Smis.
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  if (Smi::IsValid64(value)) {
    NOHANDLESCOPE(isolate);
    return Api::NewHandle(isolate, Smi::New(static_cast<intptr_t>(value)));
  }
  // Slow path for Mints and Bigints.
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);
  return Api::NewHandle(isolate, Integer::New(value));
}


DART_EXPORT Dart_Handle Dart_NewIntegerFromHexCString(const char* str) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);
  const String& str_obj = String::Handle(isolate, String::New(str));
  return Api::NewHandle(isolate, Integer::New(str_obj));
}


DART_EXPORT Dart_Handle Dart_IntegerToInt64(Dart_Handle integer,
                                            int64_t* value) {
  // Fast path for Smis.
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  if (Api::IsSmi(integer)) {
    *value = Api::SmiValue(integer);
    return Api::Success();
  }
  // Slow path for Mints and Bigints.
  DARTSCOPE(isolate);
  const Integer& int_obj = Api::UnwrapIntegerHandle(isolate, integer);
  if (int_obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, integer, Integer);
  }
  ASSERT(!int_obj.IsSmi());
  if (int_obj.IsMint()) {
    *value = int_obj.AsInt64Value();
    return Api::Success();
  } else {
    const Bigint& bigint = Bigint::Cast(int_obj);
    if (BigintOperations::FitsIntoInt64(bigint)) {
      *value = BigintOperations::ToInt64(bigint);
      return Api::Success();
    }
  }
  return Api::NewError("%s: Integer %s cannot be represented as an int64_t.",
                       CURRENT_FUNC, int_obj.ToCString());
}


DART_EXPORT Dart_Handle Dart_IntegerToUint64(Dart_Handle integer,
                                             uint64_t* value) {
  // Fast path for Smis.
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  if (Api::IsSmi(integer)) {
    intptr_t smi_value = Api::SmiValue(integer);
    if (smi_value >= 0) {
      *value = smi_value;
      return Api::Success();
    }
  }
  // Slow path for Mints and Bigints.
  DARTSCOPE(isolate);
  const Integer& int_obj = Api::UnwrapIntegerHandle(isolate, integer);
  if (int_obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, integer, Integer);
  }
  ASSERT(!int_obj.IsSmi());
  if (int_obj.IsMint() && !int_obj.IsNegative()) {
    *value = int_obj.AsInt64Value();
    return Api::Success();
  } else {
    const Bigint& bigint = Bigint::Cast(int_obj);
    if (BigintOperations::FitsIntoUint64(bigint)) {
      *value = BigintOperations::ToUint64(bigint);
      return Api::Success();
    }
  }
  return Api::NewError("%s: Integer %s cannot be represented as a uint64_t.",
                       CURRENT_FUNC, int_obj.ToCString());
}


static uword BigintAllocate(intptr_t size) {
  return Api::TopScope(Isolate::Current())->zone()->AllocUnsafe(size);
}


DART_EXPORT Dart_Handle Dart_IntegerToHexCString(Dart_Handle integer,
                                                 const char** value) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Integer& int_obj = Api::UnwrapIntegerHandle(isolate, integer);
  if (int_obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, integer, Integer);
  }
  if (int_obj.IsSmi() || int_obj.IsMint()) {
    const Bigint& bigint = Bigint::Handle(isolate,
        BigintOperations::NewFromInt64(int_obj.AsInt64Value()));
    *value = BigintOperations::ToHexCString(bigint, BigintAllocate);
  } else {
    *value = BigintOperations::ToHexCString(Bigint::Cast(int_obj),
                                            BigintAllocate);
  }
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_NewDouble(double value) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);
  return Api::NewHandle(isolate, Double::New(value));
}


DART_EXPORT Dart_Handle Dart_DoubleValue(Dart_Handle double_obj,
                                         double* value) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Double& obj = Api::UnwrapDoubleHandle(isolate, double_obj);
  if (obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, double_obj, Double);
  }
  *value = obj.value();
  return Api::Success();
}


// --- Booleans ----

DART_EXPORT Dart_Handle Dart_True() {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE_SCOPE(isolate);
  return Api::True();
}


DART_EXPORT Dart_Handle Dart_False() {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE_SCOPE(isolate);
  return Api::False();
}


DART_EXPORT Dart_Handle Dart_NewBoolean(bool value) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE_SCOPE(isolate);
  return value ? Api::True() : Api::False();
}


DART_EXPORT Dart_Handle Dart_BooleanValue(Dart_Handle boolean_obj,
                                          bool* value) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Bool& obj = Api::UnwrapBoolHandle(isolate, boolean_obj);
  if (obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, boolean_obj, Bool);
  }
  *value = obj.value();
  return Api::Success();
}


// --- Strings ---


DART_EXPORT Dart_Handle Dart_StringLength(Dart_Handle str, intptr_t* len) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const String& str_obj = Api::UnwrapStringHandle(isolate, str);
  if (str_obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, str, String);
  }
  *len = str_obj.Length();
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_NewStringFromCString(const char* str) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  if (str == NULL) {
    RETURN_NULL_ERROR(str);
  }
  CHECK_CALLBACK_STATE(isolate);
  return Api::NewHandle(isolate, String::New(str));
}


DART_EXPORT Dart_Handle Dart_NewStringFromUTF8(const uint8_t* utf8_array,
                                               intptr_t length) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  if (utf8_array == NULL && length != 0) {
    RETURN_NULL_ERROR(utf8_array);
  }
  CHECK_LENGTH(length, String::kMaxElements);
  if (!Utf8::IsValid(utf8_array, length)) {
    return Api::NewError("%s expects argument 'str' to be valid UTF-8.",
                         CURRENT_FUNC);
  }
  CHECK_CALLBACK_STATE(isolate);
  return Api::NewHandle(isolate, String::FromUTF8(utf8_array, length));
}


DART_EXPORT Dart_Handle Dart_NewStringFromUTF16(const uint16_t* utf16_array,
                                                intptr_t length) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  if (utf16_array == NULL && length != 0) {
    RETURN_NULL_ERROR(utf16_array);
  }
  CHECK_LENGTH(length, String::kMaxElements);
  CHECK_CALLBACK_STATE(isolate);
  return Api::NewHandle(isolate, String::FromUTF16(utf16_array, length));
}


DART_EXPORT Dart_Handle Dart_NewStringFromUTF32(const int32_t* utf32_array,
                                                intptr_t length) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  if (utf32_array == NULL && length != 0) {
    RETURN_NULL_ERROR(utf32_array);
  }
  CHECK_LENGTH(length, String::kMaxElements);
  CHECK_CALLBACK_STATE(isolate);
  return Api::NewHandle(isolate, String::FromUTF32(utf32_array, length));
}


DART_EXPORT Dart_Handle Dart_NewExternalLatin1String(
    const uint8_t* latin1_array,
    intptr_t length,
    void* peer,
    Dart_PeerFinalizer cback) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  if (latin1_array == NULL && length != 0) {
    RETURN_NULL_ERROR(latin1_array);
  }
  CHECK_LENGTH(length, String::kMaxElements);
  CHECK_CALLBACK_STATE(isolate);
  return Api::NewHandle(isolate,
                        String::NewExternal(latin1_array, length, peer, cback));
}


DART_EXPORT Dart_Handle Dart_NewExternalUTF16String(const uint16_t* utf16_array,
                                                    intptr_t length,
                                                    void* peer,
                                                    Dart_PeerFinalizer cback) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  if (utf16_array == NULL && length != 0) {
    RETURN_NULL_ERROR(utf16_array);
  }
  CHECK_LENGTH(length, String::kMaxElements);
  CHECK_CALLBACK_STATE(isolate);
  return Api::NewHandle(isolate,
                        String::NewExternal(utf16_array, length, peer, cback));
}


DART_EXPORT Dart_Handle Dart_StringToCString(Dart_Handle object,
                                             const char** cstr) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  if (cstr == NULL) {
    RETURN_NULL_ERROR(cstr);
  }
  const String& str_obj = Api::UnwrapStringHandle(isolate, object);
  if (str_obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, object, String);
  }
  intptr_t string_length = Utf8::Length(str_obj);
  char* res = Api::TopScope(isolate)->zone()->Alloc<char>(string_length + 1);
  if (res == NULL) {
    return Api::NewError("Unable to allocate memory");
  }
  const char* string_value = str_obj.ToCString();
  memmove(res, string_value, string_length + 1);
  ASSERT(res[string_length] == '\0');
  *cstr = res;
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_StringToUTF8(Dart_Handle str,
                                          uint8_t** utf8_array,
                                          intptr_t* length) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  if (utf8_array == NULL) {
    RETURN_NULL_ERROR(utf8_array);
  }
  if (length == NULL) {
    RETURN_NULL_ERROR(length);
  }
  const String& str_obj = Api::UnwrapStringHandle(isolate, str);
  if (str_obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, str, String);
  }
  intptr_t str_len = Utf8::Length(str_obj);
  *utf8_array = Api::TopScope(isolate)->zone()->Alloc<uint8_t>(str_len);
  if (*utf8_array == NULL) {
    return Api::NewError("Unable to allocate memory");
  }
  str_obj.ToUTF8(*utf8_array, str_len);
  *length = str_len;
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_StringToLatin1(Dart_Handle str,
                                            uint8_t* latin1_array,
                                            intptr_t* length) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  if (latin1_array == NULL) {
    RETURN_NULL_ERROR(latin1_array);
  }
  if (length == NULL) {
    RETURN_NULL_ERROR(length);
  }
  const String& str_obj = Api::UnwrapStringHandle(isolate, str);
  if (str_obj.IsNull() || !str_obj.IsOneByteString()) {
    RETURN_TYPE_ERROR(isolate, str, String);
  }
  intptr_t str_len = str_obj.Length();
  intptr_t copy_len = (str_len > *length) ? *length : str_len;

  // We have already asserted that the string object is a Latin-1 string
  // so we can copy the characters over using a simple loop.
  for (intptr_t i = 0; i < copy_len; i++) {
    latin1_array[i] = str_obj.CharAt(i);
  }
  *length = copy_len;
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_StringToUTF16(Dart_Handle str,
                                           uint16_t* utf16_array,
                                           intptr_t* length) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const String& str_obj = Api::UnwrapStringHandle(isolate, str);
  if (str_obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, str, String);
  }
  intptr_t str_len = str_obj.Length();
  intptr_t copy_len = (str_len > *length) ? *length : str_len;
  for (intptr_t i = 0; i < copy_len; i++) {
    utf16_array[i] = static_cast<uint16_t>(str_obj.CharAt(i));
  }
  *length = copy_len;
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_StringStorageSize(Dart_Handle str,
                                               intptr_t* size) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const String& str_obj = Api::UnwrapStringHandle(isolate, str);
  if (str_obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, str, String);
  }
  if (size == NULL) {
    RETURN_NULL_ERROR(size);
  }
  *size = (str_obj.Length() * str_obj.CharSize());
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_MakeExternalString(Dart_Handle str,
                                                void* array,
                                                intptr_t length,
                                                void* peer,
                                                Dart_PeerFinalizer cback) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const String& str_obj = Api::UnwrapStringHandle(isolate, str);
  if (str_obj.IsExternal()) {
    return str;  // String is already an external string.
  }
  if (str_obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, str, String);
  }
  if (array == NULL) {
    RETURN_NULL_ERROR(array);
  }
  intptr_t str_size = (str_obj.Length() * str_obj.CharSize());
  if ((length < str_size) || (length > String::kMaxElements)) {
    return Api::NewError("Dart_MakeExternalString "
                         "expects argument length to be in the range"
                         "[%" Pd "..%" Pd "].",
                         str_size, String::kMaxElements);
  }
  if (str_obj.InVMHeap()) {
    // Since the string object is read only we do not externalize
    // the string but instead copy the contents of the string into the
    // specified buffer add the specified peer/cback as a Peer object
    // to this string. The Api::StringGetPeerHelper function picks up
    // the peer from the Peer table.
    intptr_t copy_len = str_obj.Length();
    if (str_obj.IsOneByteString()) {
      ASSERT(length >= copy_len);
      uint8_t* latin1_array = reinterpret_cast<uint8_t*>(array);
      for (intptr_t i = 0; i < copy_len; i++) {
        latin1_array[i] = static_cast<uint8_t>(str_obj.CharAt(i));
      }
      OneByteString::SetPeer(str_obj, peer, cback);
    } else {
      ASSERT(str_obj.IsTwoByteString());
      ASSERT(length >= (copy_len * str_obj.CharSize()));
      uint16_t* utf16_array = reinterpret_cast<uint16_t*>(array);
      for (intptr_t i = 0; i < copy_len; i++) {
        utf16_array[i] = static_cast<uint16_t>(str_obj.CharAt(i));
      }
      TwoByteString::SetPeer(str_obj, peer, cback);
    }
    return str;
  }
  return Api::NewHandle(isolate,
                        str_obj.MakeExternal(array, length, peer, cback));
}


DART_EXPORT Dart_Handle Dart_StringGetProperties(Dart_Handle object,
                                                 intptr_t* char_size,
                                                 intptr_t* str_len,
                                                 void** peer) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const String& str = Api::UnwrapStringHandle(isolate, object);
  if (str.IsNull()) {
    RETURN_TYPE_ERROR(isolate, object, String);
  }
  if (str.IsExternal()) {
    *peer = str.GetPeer();
    ASSERT(*peer != NULL);
  } else {
    NoGCScope no_gc_scope;
    *peer = isolate->heap()->GetPeer(str.raw());
  }
  *char_size = str.CharSize();
  *str_len = str.Length();
  return Api::Success();
}


// --- Lists ---

DART_EXPORT Dart_Handle Dart_NewList(intptr_t length) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_LENGTH(length, Array::kMaxElements);
  CHECK_CALLBACK_STATE(isolate);
  return Api::NewHandle(isolate, Array::New(length));
}


#define GET_LIST_LENGTH(isolate, type, obj, len)                               \
  type& array = type::Handle(isolate);                                         \
  array ^= obj.raw();                                                          \
  *len = array.Length();                                                       \
  return Api::Success();                                                       \


DART_EXPORT Dart_Handle Dart_ListLength(Dart_Handle list, intptr_t* len) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(list));
  if (obj.IsError()) {
    // Pass through errors.
    return list;
  }
  if (obj.IsTypedData()) {
    GET_LIST_LENGTH(isolate, TypedData, obj, len);
  }
  if (obj.IsArray()) {
    GET_LIST_LENGTH(isolate, Array, obj, len);
  }
  if (obj.IsGrowableObjectArray()) {
    GET_LIST_LENGTH(isolate, GrowableObjectArray, obj, len);
  }
  if (obj.IsExternalTypedData()) {
    GET_LIST_LENGTH(isolate, ExternalTypedData, obj, len);
  }
  CHECK_CALLBACK_STATE(isolate);

  // Now check and handle a dart object that implements the List interface.
  const Instance& instance =
      Instance::Handle(isolate, GetListInstance(isolate, obj));
  if (instance.IsNull()) {
    return Api::NewError("Object does not implement the List interface");
  }
  const String& name = String::Handle(Field::GetterName(Symbols::Length()));
  const int kNumArgs = 1;
  ArgumentsDescriptor args_desc(
      Array::Handle(ArgumentsDescriptor::New(kNumArgs)));
  const Function& function =
      Function::Handle(isolate, Resolver::ResolveDynamic(instance,
                                                         name,
                                                         args_desc));
  if (function.IsNull()) {
    return Api::NewError("List object does not have a 'length' field.");
  }

  const Array& args = Array::Handle(isolate, Array::New(kNumArgs));
  args.SetAt(0, instance);  // Set up the receiver as the first argument.
  const Object& retval =
    Object::Handle(isolate, DartEntry::InvokeFunction(function, args));
  if (retval.IsSmi()) {
    *len = Smi::Cast(retval).Value();
    return Api::Success();
  } else if (retval.IsMint() || retval.IsBigint()) {
    if (retval.IsMint()) {
      int64_t mint_value = Mint::Cast(retval).value();
      if (mint_value >= kIntptrMin && mint_value <= kIntptrMax) {
        *len = static_cast<intptr_t>(mint_value);
      }
    } else {
      // Check for a non-canonical Mint range value.
      ASSERT(retval.IsBigint());
      const Bigint& bigint = Bigint::Handle();
      if (BigintOperations::FitsIntoInt64(bigint)) {
        int64_t bigint_value = bigint.AsInt64Value();
        if (bigint_value >= kIntptrMin && bigint_value <= kIntptrMax) {
          *len = static_cast<intptr_t>(bigint_value);
        }
      }
    }
    return Api::NewError("Length of List object is greater than the "
                         "maximum value that 'len' parameter can hold");
  } else if (retval.IsError()) {
    return Api::NewHandle(isolate, retval.raw());
  } else {
    return Api::NewError("Length of List object is not an integer");
  }
}


#define GET_LIST_ELEMENT(isolate, type, obj, index)                            \
  const type& array_obj = type::Cast(obj);                                     \
  if ((index >= 0) && (index < array_obj.Length())) {                          \
    return Api::NewHandle(isolate, array_obj.At(index));                       \
  }                                                                            \
  return Api::NewError("Invalid index passed in to access list element");      \


DART_EXPORT Dart_Handle Dart_ListGetAt(Dart_Handle list, intptr_t index) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(list));
  if (obj.IsArray()) {
    GET_LIST_ELEMENT(isolate, Array, obj, index);
  } else if (obj.IsGrowableObjectArray()) {
    GET_LIST_ELEMENT(isolate, GrowableObjectArray, obj, index);
  } else if (obj.IsError()) {
    return list;
  } else {
    CHECK_CALLBACK_STATE(isolate);

    // Check and handle a dart object that implements the List interface.
    const Instance& instance =
        Instance::Handle(isolate, GetListInstance(isolate, obj));
    if (!instance.IsNull()) {
      const int kNumArgs = 2;
      ArgumentsDescriptor args_desc(
          Array::Handle(ArgumentsDescriptor::New(kNumArgs)));
      const Function& function = Function::Handle(
          isolate,
          Resolver::ResolveDynamic(instance, Symbols::IndexToken(), args_desc));
      if (!function.IsNull()) {
        const Array& args = Array::Handle(isolate, Array::New(kNumArgs));
        const Integer& indexobj = Integer::Handle(isolate, Integer::New(index));
        args.SetAt(0, instance);
        args.SetAt(1, indexobj);
        return Api::NewHandle(isolate, DartEntry::InvokeFunction(function,
                                                                 args));
      }
    }
    return Api::NewError("Object does not implement the 'List' interface");
  }
}


#define SET_LIST_ELEMENT(isolate, type, obj, index, value)                     \
  const type& array = type::Cast(obj);                                         \
  const Object& value_obj = Object::Handle(isolate, Api::UnwrapHandle(value)); \
  if (!value_obj.IsNull() && !value_obj.IsInstance()) {                        \
    RETURN_TYPE_ERROR(isolate, value, Instance);                               \
  }                                                                            \
  if ((index >= 0) && (index < array.Length())) {                              \
    array.SetAt(index, value_obj);                                             \
    return Api::Success();                                                     \
  }                                                                            \
  return Api::NewError("Invalid index passed in to set list element");         \


DART_EXPORT Dart_Handle Dart_ListSetAt(Dart_Handle list,
                                       intptr_t index,
                                       Dart_Handle value) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(list));
  // If the list is immutable we call into Dart for the indexed setter to
  // get the unsupported operation exception as the result.
  if (obj.IsArray() && !Array::Cast(obj).IsImmutable()) {
    SET_LIST_ELEMENT(isolate, Array, obj, index, value);
  } else if (obj.IsGrowableObjectArray()) {
    SET_LIST_ELEMENT(isolate, GrowableObjectArray, obj, index, value);
  } else if (obj.IsError()) {
    return list;
  } else {
    CHECK_CALLBACK_STATE(isolate);

    // Check and handle a dart object that implements the List interface.
    const Instance& instance =
        Instance::Handle(isolate, GetListInstance(isolate, obj));
    if (!instance.IsNull()) {
      const intptr_t kNumArgs = 3;
      ArgumentsDescriptor args_desc(
          Array::Handle(ArgumentsDescriptor::New(kNumArgs)));
      const Function& function = Function::Handle(
          isolate,
          Resolver::ResolveDynamic(instance,
                                   Symbols::AssignIndexToken(),
                                   args_desc));
      if (!function.IsNull()) {
        const Integer& index_obj =
            Integer::Handle(isolate, Integer::New(index));
        const Object& value_obj =
            Object::Handle(isolate, Api::UnwrapHandle(value));
        if (!value_obj.IsNull() && !value_obj.IsInstance()) {
          RETURN_TYPE_ERROR(isolate, value, Instance);
        }
        const Array& args = Array::Handle(isolate, Array::New(kNumArgs));
        args.SetAt(0, instance);
        args.SetAt(1, index_obj);
        args.SetAt(2, value_obj);
        return Api::NewHandle(isolate, DartEntry::InvokeFunction(function,
                                                                 args));
      }
    }
    return Api::NewError("Object does not implement the 'List' interface");
  }
}


static RawObject* ResolveConstructor(const char* current_func,
                                     const Class& cls,
                                     const String& class_name,
                                     const String& dotted_name,
                                     int num_args);


static RawObject* ThrowArgumentError(const char* exception_message) {
  Isolate* isolate = Isolate::Current();
  // Lookup the class ArgumentError in dart:core.
  const String& lib_url = String::Handle(String::New("dart:core"));
  const String& class_name = String::Handle(String::New("ArgumentError"));
  const Library& lib =
      Library::Handle(isolate, Library::LookupLibrary(lib_url));
  if (lib.IsNull()) {
    const String& message = String::Handle(
        String::NewFormatted("%s: library '%s' not found.",
                             CURRENT_FUNC, lib_url.ToCString()));
    return ApiError::New(message);
  }
  const Class& cls = Class::Handle(
      isolate, lib.LookupClassAllowPrivate(class_name));
  ASSERT(!cls.IsNull());
  Object& result = Object::Handle(isolate);
  String& dot_name = String::Handle(String::New("."));
  String& constr_name = String::Handle(String::Concat(class_name, dot_name));
  result = ResolveConstructor(CURRENT_FUNC, cls, class_name, constr_name, 1);
  if (result.IsError()) return result.raw();
  ASSERT(result.IsFunction());
  Function& constructor = Function::Handle(isolate);
  constructor ^= result.raw();
  if (!constructor.IsConstructor()) {
    const String& message = String::Handle(
        String::NewFormatted("%s: class '%s' is not a constructor.",
                             CURRENT_FUNC, class_name.ToCString()));
    return ApiError::New(message);
  }
  Instance& exception = Instance::Handle(isolate);
  exception = Instance::New(cls);
  const Array& args = Array::Handle(isolate, Array::New(3));
  args.SetAt(0, exception);
  args.SetAt(1,
             Smi::Handle(isolate, Smi::New(Function::kCtorPhaseAll)));
  args.SetAt(2, String::Handle(String::New(exception_message)));
  result = DartEntry::InvokeFunction(constructor, args);
  if (result.IsError()) return result.raw();
  ASSERT(result.IsNull());

  if (isolate->top_exit_frame_info() == 0) {
    // There are no dart frames on the stack so it would be illegal to
    // throw an exception here.
    const String& message = String::Handle(
            String::New("No Dart frames on stack, cannot throw exception"));
    return ApiError::New(message);
  }
  // Unwind all the API scopes till the exit frame before throwing an
  // exception.
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  const Instance* saved_exception;
  {
    NoGCScope no_gc;
    RawInstance* raw_exception = exception.raw();
    state->UnwindScopes(isolate->top_exit_frame_info());
    saved_exception = &Instance::Handle(raw_exception);
  }
  Exceptions::Throw(*saved_exception);
  const String& message = String::Handle(
          String::New("Exception was not thrown, internal error"));
  return ApiError::New(message);
}

// TODO(sgjesse): value should always be smaller then 0xff. Add error handling.
#define GET_LIST_ELEMENT_AS_BYTES(isolate, type, obj, native_array, offset,    \
                                  length)                                      \
  const type& array = type::Cast(obj);                                         \
  if (Utils::RangeCheck(offset, length, array.Length())) {                     \
    Object& element = Object::Handle(isolate);                                 \
    for (int i = 0; i < length; i++) {                                         \
      element = array.At(offset + i);                                          \
      if (!element.IsInteger()) {                                              \
        return Api::NewHandle(                                                 \
            isolate, ThrowArgumentError("List contains non-int elements"));    \
                                                                               \
      }                                                                        \
      const Integer& integer = Integer::Cast(element);                         \
      native_array[i] = static_cast<uint8_t>(integer.AsInt64Value() & 0xff);   \
      ASSERT(integer.AsInt64Value() <= 0xff);                                  \
    }                                                                          \
    return Api::Success();                                                     \
  }                                                                            \
  return Api::NewError("Invalid length passed in to access array elements");   \


static Dart_Handle CopyBytes(const TypedData& array,
                             intptr_t offset,
                             uint8_t* native_array,
                             intptr_t length) {
  ASSERT(array.IsTypedData());
  ASSERT(array.ElementSizeInBytes() == 1);
  NoGCScope no_gc;
  memmove(native_array,
          reinterpret_cast<uint8_t*>(array.DataAddr(offset)),
          length);
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_ListGetAsBytes(Dart_Handle list,
                                            intptr_t offset,
                                            uint8_t* native_array,
                                            intptr_t length) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(list));
  if (obj.IsTypedData()) {
    const TypedData& array = TypedData::Cast(obj);
    if (array.ElementSizeInBytes() == 1) {
      if (!Utils::RangeCheck(offset, length, array.Length())) {
        return Api::NewError(
            "Invalid length passed in to access list elements");
      }
      return CopyBytes(array, offset, native_array, length);
    }
  }
  if (RawObject::IsTypedDataViewClassId(obj.GetClassId())) {
    const Instance& view = Instance::Cast(obj);
    if (TypedDataView::ElementSizeInBytes(view) == 1) {
      intptr_t view_length = Smi::Value(TypedDataView::Length(view));
      if (!Utils::RangeCheck(offset, length, view_length)) {
        return Api::NewError(
            "Invalid length passed in to access list elements");
      }
      const Instance& data = Instance::Handle(TypedDataView::Data(view));
      if (data.IsTypedData()) {
        const TypedData& array = TypedData::Cast(data);
        if (array.ElementSizeInBytes() == 1) {
          intptr_t data_offset =
              Smi::Value(TypedDataView::OffsetInBytes(view)) + offset;
          // Range check already performed on the view object.
          ASSERT(Utils::RangeCheck(data_offset, length, array.Length()));
          return CopyBytes(array, data_offset, native_array, length);
        }
      }
    }
  }
  if (obj.IsArray()) {
    GET_LIST_ELEMENT_AS_BYTES(isolate,
                              Array,
                              obj,
                              native_array,
                              offset,
                              length);
  }
  if (obj.IsGrowableObjectArray()) {
    GET_LIST_ELEMENT_AS_BYTES(isolate,
                              GrowableObjectArray,
                              obj,
                              native_array,
                              offset,
                              length);
  }
  if (obj.IsError()) {
    return list;
  }
  CHECK_CALLBACK_STATE(isolate);

  // Check and handle a dart object that implements the List interface.
  const Instance& instance =
      Instance::Handle(isolate, GetListInstance(isolate, obj));
  if (!instance.IsNull()) {
    const int kNumArgs = 2;
    ArgumentsDescriptor args_desc(
        Array::Handle(ArgumentsDescriptor::New(kNumArgs)));
    const Function& function = Function::Handle(
        isolate,
        Resolver::ResolveDynamic(instance, Symbols::IndexToken(), args_desc));
    if (!function.IsNull()) {
      Object& result = Object::Handle(isolate);
      Integer& intobj = Integer::Handle(isolate);
      const Array& args = Array::Handle(isolate, Array::New(kNumArgs));
      args.SetAt(0, instance);  // Set up the receiver as the first argument.
      for (int i = 0; i < length; i++) {
        HANDLESCOPE(isolate);
        intobj = Integer::New(offset + i);
        args.SetAt(1, intobj);
        result = DartEntry::InvokeFunction(function, args);
        if (result.IsError()) {
          return Api::NewHandle(isolate, result.raw());
        }
        if (!result.IsInteger()) {
          return Api::NewError("%s expects the argument 'list' to be "
                               "a List of int", CURRENT_FUNC);
        }
        const Integer& integer_result = Integer::Cast(result);
        ASSERT(integer_result.AsInt64Value() <= 0xff);
        // TODO(hpayer): value should always be smaller then 0xff. Add error
        // handling.
        native_array[i] =
            static_cast<uint8_t>(integer_result.AsInt64Value() & 0xff);
      }
      return Api::Success();
    }
  }
  return Api::NewError("Object does not implement the 'List' interface");
}


#define SET_LIST_ELEMENT_AS_BYTES(isolate, type, obj, native_array, offset,    \
                                  length)                                      \
  const type& array = type::Cast(obj);                                         \
  Integer& integer = Integer::Handle(isolate);                                 \
  if (Utils::RangeCheck(offset, length, array.Length())) {                     \
    for (int i = 0; i < length; i++) {                                         \
      integer = Integer::New(native_array[i]);                                 \
      array.SetAt(offset + i, integer);                                        \
    }                                                                          \
    return Api::Success();                                                     \
  }                                                                            \
  return Api::NewError("Invalid length passed in to set array elements");      \


DART_EXPORT Dart_Handle Dart_ListSetAsBytes(Dart_Handle list,
                                            intptr_t offset,
                                            uint8_t* native_array,
                                            intptr_t length) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(list));
  if (obj.IsTypedData()) {
    const TypedData& array = TypedData::Cast(obj);
    if (array.ElementSizeInBytes() == 1) {
      if (Utils::RangeCheck(offset, length, array.Length())) {
        NoGCScope no_gc;
        memmove(reinterpret_cast<uint8_t*>(array.DataAddr(offset)),
                native_array,
                length);
        return Api::Success();
      }
      return Api::NewError("Invalid length passed in to access list elements");
    }
  }
  if (obj.IsArray() && !Array::Cast(obj).IsImmutable()) {
    // If the list is immutable we call into Dart for the indexed setter to
    // get the unsupported operation exception as the result.
    SET_LIST_ELEMENT_AS_BYTES(isolate,
                              Array,
                              obj,
                              native_array,
                              offset,
                              length);
  }
  if (obj.IsGrowableObjectArray()) {
    SET_LIST_ELEMENT_AS_BYTES(isolate,
                              GrowableObjectArray,
                              obj,
                              native_array,
                              offset,
                              length);
  }
  if (obj.IsError()) {
    return list;
  }
  CHECK_CALLBACK_STATE(isolate);

  // Check and handle a dart object that implements the List interface.
  const Instance& instance =
      Instance::Handle(isolate, GetListInstance(isolate, obj));
  if (!instance.IsNull()) {
    const int kNumArgs = 3;
    ArgumentsDescriptor args_desc(
        Array::Handle(ArgumentsDescriptor::New(kNumArgs)));
    const Function& function = Function::Handle(
        isolate,
        Resolver::ResolveDynamic(instance,
                                 Symbols::AssignIndexToken(),
                                 args_desc));
    if (!function.IsNull()) {
      Integer& indexobj = Integer::Handle(isolate);
      Integer& valueobj = Integer::Handle(isolate);
      const Array& args = Array::Handle(isolate, Array::New(kNumArgs));
      args.SetAt(0, instance);  // Set up the receiver as the first argument.
      for (int i = 0; i < length; i++) {
        indexobj = Integer::New(offset + i);
        valueobj = Integer::New(native_array[i]);
        args.SetAt(1, indexobj);
        args.SetAt(2, valueobj);
        const Object& result = Object::Handle(
            isolate, DartEntry::InvokeFunction(function, args));
        if (result.IsError()) {
          return Api::NewHandle(isolate, result.raw());
        }
      }
      return Api::Success();
    }
  }
  return Api::NewError("Object does not implement the 'List' interface");
}


// --- Typed Data ---

// Helper method to get the type of a TypedData object.
static Dart_TypedData_Type GetType(intptr_t class_id) {
  Dart_TypedData_Type type;
  switch (class_id) {
    case kByteDataViewCid :
      type = Dart_TypedData_kByteData;
      break;
    case kTypedDataInt8ArrayCid :
    case kTypedDataInt8ArrayViewCid :
    case kExternalTypedDataInt8ArrayCid :
      type = Dart_TypedData_kInt8;
      break;
    case kTypedDataUint8ArrayCid :
    case kTypedDataUint8ArrayViewCid :
    case kExternalTypedDataUint8ArrayCid :
      type = Dart_TypedData_kUint8;
      break;
    case kTypedDataUint8ClampedArrayCid :
    case kTypedDataUint8ClampedArrayViewCid :
    case kExternalTypedDataUint8ClampedArrayCid :
      type = Dart_TypedData_kUint8Clamped;
      break;
    case kTypedDataInt16ArrayCid :
    case kTypedDataInt16ArrayViewCid :
    case kExternalTypedDataInt16ArrayCid :
      type = Dart_TypedData_kInt16;
      break;
    case kTypedDataUint16ArrayCid :
    case kTypedDataUint16ArrayViewCid :
    case kExternalTypedDataUint16ArrayCid :
      type = Dart_TypedData_kUint16;
      break;
    case kTypedDataInt32ArrayCid :
    case kTypedDataInt32ArrayViewCid :
    case kExternalTypedDataInt32ArrayCid :
      type = Dart_TypedData_kInt32;
      break;
    case kTypedDataUint32ArrayCid :
    case kTypedDataUint32ArrayViewCid :
    case kExternalTypedDataUint32ArrayCid :
      type = Dart_TypedData_kUint32;
      break;
    case kTypedDataInt64ArrayCid :
    case kTypedDataInt64ArrayViewCid :
    case kExternalTypedDataInt64ArrayCid :
      type = Dart_TypedData_kInt64;
      break;
    case kTypedDataUint64ArrayCid :
    case kTypedDataUint64ArrayViewCid :
    case kExternalTypedDataUint64ArrayCid :
      type = Dart_TypedData_kUint64;
      break;
    case kTypedDataFloat32ArrayCid :
    case kTypedDataFloat32ArrayViewCid :
    case kExternalTypedDataFloat32ArrayCid :
      type = Dart_TypedData_kFloat32;
      break;
    case kTypedDataFloat64ArrayCid :
    case kTypedDataFloat64ArrayViewCid :
    case kExternalTypedDataFloat64ArrayCid :
      type = Dart_TypedData_kFloat64;
      break;
    case kTypedDataFloat32x4ArrayCid :
    case kTypedDataFloat32x4ArrayViewCid :
    case kExternalTypedDataFloat32x4ArrayCid :
      type = Dart_TypedData_kFloat32x4;
      break;
    default:
      type = Dart_TypedData_kInvalid;
      break;
  }
  return type;
}


DART_EXPORT Dart_TypedData_Type Dart_GetTypeOfTypedData(Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  intptr_t class_id = Api::ClassId(object);
  if (RawObject::IsTypedDataClassId(class_id) ||
      RawObject::IsTypedDataViewClassId(class_id)) {
    return GetType(class_id);
  }
  return Dart_TypedData_kInvalid;
}


DART_EXPORT Dart_TypedData_Type Dart_GetTypeOfExternalTypedData(
    Dart_Handle object) {
  TRACE_API_CALL(CURRENT_FUNC);
  intptr_t class_id = Api::ClassId(object);
  if (RawObject::IsExternalTypedDataClassId(class_id) ||
      RawObject::IsTypedDataViewClassId(class_id)) {
    return GetType(class_id);
  }
  return Dart_TypedData_kInvalid;
}


static RawObject* GetByteDataConstructor(Isolate* isolate,
                                         const String& constructor_name,
                                         intptr_t num_args) {
  const Library& lib =
      Library::Handle(isolate->object_store()->typed_data_library());
  ASSERT(!lib.IsNull());
  const Class& cls = Class::Handle(
      isolate, lib.LookupClassAllowPrivate(Symbols::ByteData()));
  ASSERT(!cls.IsNull());
  return ResolveConstructor(CURRENT_FUNC,
                            cls,
                            Symbols::ByteData(),
                            constructor_name,
                            num_args);
}


static Dart_Handle NewByteData(Isolate* isolate, intptr_t length) {
  CHECK_LENGTH(length, TypedData::MaxElements(kTypedDataInt8ArrayCid));
  Object& result = Object::Handle(isolate);
  result = GetByteDataConstructor(isolate, Symbols::ByteDataDot(), 1);
  ASSERT(!result.IsNull());
  ASSERT(result.IsFunction());
  const Function& factory = Function::Cast(result);
  ASSERT(!factory.IsConstructor());

  // Create the argument list.
  const Array& args = Array::Handle(isolate, Array::New(2));
  // Factories get type arguments.
  args.SetAt(0, TypeArguments::Handle(isolate));
  args.SetAt(1, Smi::Handle(isolate, Smi::New(length)));

  // Invoke the constructor and return the new object.
  result = DartEntry::InvokeFunction(factory, args);
  ASSERT(result.IsInstance() || result.IsNull() || result.IsError());
  return Api::NewHandle(isolate, result.raw());
}


static Dart_Handle NewTypedData(Isolate* isolate,
                                intptr_t cid,
                                intptr_t length) {
  CHECK_LENGTH(length, TypedData::MaxElements(cid));
  return Api::NewHandle(isolate, TypedData::New(cid, length));
}


static Dart_Handle NewExternalTypedData(
    Isolate* isolate, intptr_t cid, void* data, intptr_t length) {
  CHECK_LENGTH(length, ExternalTypedData::MaxElements(cid));
  const ExternalTypedData& result = ExternalTypedData::Handle(
      isolate,
      ExternalTypedData::New(cid, reinterpret_cast<uint8_t*>(data), length));
  return Api::NewHandle(isolate, result.raw());
}


static Dart_Handle NewExternalByteData(
    Isolate* isolate, void* data, intptr_t length) {
  Dart_Handle ext_data = NewExternalTypedData(
      isolate, kExternalTypedDataUint8ArrayCid, data, length);
  if (::Dart_IsError(ext_data)) {
    return ext_data;
  }
  Object& result = Object::Handle(isolate);
  result = GetByteDataConstructor(isolate, Symbols::ByteDataDotview(), 3);
  ASSERT(!result.IsNull());
  ASSERT(result.IsFunction());
  const Function& factory = Function::Cast(result);
  ASSERT(!factory.IsConstructor());

  // Create the argument list.
  const intptr_t num_args = 3;
  const Array& args = Array::Handle(isolate, Array::New(num_args + 1));
  // Factories get type arguments.
  args.SetAt(0, TypeArguments::Handle(isolate));
  const ExternalTypedData& array =
      Api::UnwrapExternalTypedDataHandle(isolate, ext_data);
  args.SetAt(1, array);
  Smi& smi = Smi::Handle(isolate);
  smi = Smi::New(0);
  args.SetAt(2, smi);
  smi = Smi::New(length);
  args.SetAt(3, smi);

  // Invoke the constructor and return the new object.
  result = DartEntry::InvokeFunction(factory, args);
  ASSERT(result.IsNull() || result.IsInstance() || result.IsError());
  return Api::NewHandle(isolate, result.raw());
}


DART_EXPORT Dart_Handle Dart_NewTypedData(Dart_TypedData_Type type,
                                          intptr_t length) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);
  switch (type) {
    case Dart_TypedData_kByteData :
      return NewByteData(isolate, length);
    case Dart_TypedData_kInt8 :
      return NewTypedData(isolate, kTypedDataInt8ArrayCid, length);
    case Dart_TypedData_kUint8 :
      return NewTypedData(isolate, kTypedDataUint8ArrayCid, length);
    case Dart_TypedData_kUint8Clamped :
      return NewTypedData(isolate, kTypedDataUint8ClampedArrayCid, length);
    case Dart_TypedData_kInt16 :
      return NewTypedData(isolate, kTypedDataInt16ArrayCid, length);
    case Dart_TypedData_kUint16 :
      return NewTypedData(isolate, kTypedDataUint16ArrayCid, length);
    case Dart_TypedData_kInt32 :
      return NewTypedData(isolate, kTypedDataInt32ArrayCid, length);
    case Dart_TypedData_kUint32 :
      return NewTypedData(isolate, kTypedDataUint32ArrayCid, length);
    case Dart_TypedData_kInt64 :
      return NewTypedData(isolate, kTypedDataInt64ArrayCid, length);
    case Dart_TypedData_kUint64 :
      return NewTypedData(isolate, kTypedDataUint64ArrayCid, length);
    case Dart_TypedData_kFloat32 :
      return NewTypedData(isolate, kTypedDataFloat32ArrayCid,  length);
    case Dart_TypedData_kFloat64 :
      return NewTypedData(isolate, kTypedDataFloat64ArrayCid, length);
    case Dart_TypedData_kFloat32x4:
      return NewTypedData(isolate, kTypedDataFloat32x4ArrayCid, length);
    default:
      return Api::NewError("%s expects argument 'type' to be of 'TypedData'",
                           CURRENT_FUNC);
  }
  UNREACHABLE();
  return Api::Null();
}


DART_EXPORT Dart_Handle Dart_NewExternalTypedData(
    Dart_TypedData_Type type,
    void* data,
    intptr_t length) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  if (data == NULL && length != 0) {
    RETURN_NULL_ERROR(data);
  }
  CHECK_CALLBACK_STATE(isolate);
  switch (type) {
    case Dart_TypedData_kByteData:
      return NewExternalByteData(isolate, data, length);
    case Dart_TypedData_kInt8:
      return NewExternalTypedData(isolate,
                                  kExternalTypedDataInt8ArrayCid,
                                  data,
                                  length);
    case Dart_TypedData_kUint8:
      return NewExternalTypedData(isolate,
                                  kExternalTypedDataUint8ArrayCid,
                                  data,
                                  length);
    case Dart_TypedData_kUint8Clamped:
      return NewExternalTypedData(isolate,
                                  kExternalTypedDataUint8ClampedArrayCid,
                                  data,
                                  length);
    case Dart_TypedData_kInt16:
      return NewExternalTypedData(isolate,
                                  kExternalTypedDataInt16ArrayCid,
                                  data,
                                  length);
    case Dart_TypedData_kUint16:
      return NewExternalTypedData(isolate,
                                  kExternalTypedDataUint16ArrayCid,
                                  data,
                                  length);
    case Dart_TypedData_kInt32:
      return NewExternalTypedData(isolate,
                                  kExternalTypedDataInt32ArrayCid,
                                  data,
                                  length);
    case Dart_TypedData_kUint32:
      return NewExternalTypedData(isolate,
                                  kExternalTypedDataUint32ArrayCid,
                                  data,
                                  length);
    case Dart_TypedData_kInt64:
      return NewExternalTypedData(isolate,
                                  kExternalTypedDataInt64ArrayCid,
                                  data,
                                  length);
    case Dart_TypedData_kUint64:
      return NewExternalTypedData(isolate,
                                  kExternalTypedDataUint64ArrayCid,
                                  data,
                                  length);
    case Dart_TypedData_kFloat32:
      return NewExternalTypedData(isolate,
                                  kExternalTypedDataFloat32ArrayCid,
                                  data,
                                  length);
    case Dart_TypedData_kFloat64:
      return NewExternalTypedData(isolate,
                                  kExternalTypedDataFloat64ArrayCid,
                                  data,
                                  length);
    case Dart_TypedData_kFloat32x4:
      return NewExternalTypedData(isolate,
                                  kExternalTypedDataFloat32x4ArrayCid,
                                  data,
                                  length);
    default:
      return Api::NewError("%s expects argument 'type' to be of"
                           " 'external TypedData'", CURRENT_FUNC);
  }
  UNREACHABLE();
  return Api::Null();
}


DART_EXPORT Dart_Handle Dart_TypedDataAcquireData(Dart_Handle object,
                                                  Dart_TypedData_Type* type,
                                                  void** data,
                                                  intptr_t* len) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  intptr_t class_id = Api::ClassId(object);
  if (!RawObject::IsExternalTypedDataClassId(class_id) &&
      !RawObject::IsTypedDataViewClassId(class_id) &&
      !RawObject::IsTypedDataClassId(class_id)) {
    RETURN_TYPE_ERROR(isolate, object, 'TypedData');
  }
  if (type == NULL) {
    RETURN_NULL_ERROR(type);
  }
  if (data == NULL) {
    RETURN_NULL_ERROR(data);
  }
  if (len == NULL) {
    RETURN_NULL_ERROR(len);
  }
  // Get the type of typed data object.
  *type = GetType(class_id);
  // If it is an external typed data object just return the data field.
  if (RawObject::IsExternalTypedDataClassId(class_id)) {
    const ExternalTypedData& obj =
        Api::UnwrapExternalTypedDataHandle(isolate, object);
    ASSERT(!obj.IsNull());
    *len = obj.Length();
    *data = obj.DataAddr(0);
  } else if (RawObject::IsTypedDataClassId(class_id)) {
    // Regular typed data object, set up some GC and API callback guards.
    const TypedData& obj = Api::UnwrapTypedDataHandle(isolate, object);
    ASSERT(!obj.IsNull());
    *len = obj.Length();
    isolate->IncrementNoGCScopeDepth();
    START_NO_CALLBACK_SCOPE(isolate);
    *data = obj.DataAddr(0);
  } else {
    ASSERT(RawObject::IsTypedDataViewClassId(class_id));
    const Instance& view_obj = Api::UnwrapInstanceHandle(isolate, object);
    ASSERT(!view_obj.IsNull());
    Smi& val = Smi::Handle();
    val ^= TypedDataView::Length(view_obj);
    *len = val.Value();
    val ^= TypedDataView::OffsetInBytes(view_obj);
    intptr_t offset_in_bytes = val.Value();
    const Instance& obj = Instance::Handle(TypedDataView::Data(view_obj));
    isolate->IncrementNoGCScopeDepth();
    START_NO_CALLBACK_SCOPE(isolate);
    if (TypedData::IsTypedData(obj)) {
      const TypedData& data_obj = TypedData::Cast(obj);
      *data = data_obj.DataAddr(offset_in_bytes);
    } else {
      ASSERT(ExternalTypedData::IsExternalTypedData(obj));
      const ExternalTypedData& data_obj = ExternalTypedData::Cast(obj);
      *data = data_obj.DataAddr(offset_in_bytes);
    }
  }
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_TypedDataReleaseData(Dart_Handle object) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  intptr_t class_id = Api::ClassId(object);
  if (!RawObject::IsExternalTypedDataClassId(class_id) &&
      !RawObject::IsTypedDataViewClassId(class_id) &&
      !RawObject::IsTypedDataClassId(class_id)) {
    RETURN_TYPE_ERROR(isolate, object, 'TypedData');
  }
  if (!RawObject::IsExternalTypedDataClassId(class_id)) {
    isolate->DecrementNoGCScopeDepth();
    END_NO_CALLBACK_SCOPE(isolate);
  }
  return Api::Success();
}


// ---  Invoking Constructors, Methods, and Field accessors ---

static RawObject* ResolveConstructor(const char* current_func,
                                     const Class& cls,
                                     const String& class_name,
                                     const String& constr_name,
                                     int num_args) {
  // The constructor must be present in the interface.
  const Function& constructor =
      Function::Handle(cls.LookupFunctionAllowPrivate(constr_name));
  if (constructor.IsNull() ||
      (!constructor.IsConstructor() && !constructor.IsFactory())) {
    const String& lookup_class_name = String::Handle(cls.Name());
    if (!class_name.Equals(lookup_class_name)) {
      // When the class name used to build the constructor name is
      // different than the name of the class in which we are doing
      // the lookup, it can be confusing to the user to figure out
      // what's going on.  Be a little more explicit for these error
      // messages.
      const String& message = String::Handle(
          String::NewFormatted(
              "%s: could not find factory '%s' in class '%s'.",
              current_func,
              constr_name.ToCString(),
              lookup_class_name.ToCString()));
      return ApiError::New(message);
    } else {
      const String& message = String::Handle(
          String::NewFormatted("%s: could not find constructor '%s'.",
                               current_func, constr_name.ToCString()));
      return ApiError::New(message);
    }
  }
  int extra_args = (constructor.IsConstructor() ? 2 : 1);
  String& error_message = String::Handle();
  if (!constructor.AreValidArgumentCounts(num_args + extra_args,
                                          0,
                                          &error_message)) {
    const String& message = String::Handle(
        String::NewFormatted("%s: wrong argument count for "
                             "constructor '%s': %s.",
                             current_func,
                             constr_name.ToCString(),
                             error_message.ToCString()));
    return ApiError::New(message);
  }
  return constructor.raw();
}


DART_EXPORT Dart_Handle Dart_New(Dart_Handle type,
                                 Dart_Handle constructor_name,
                                 int number_of_arguments,
                                 Dart_Handle* arguments) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);
  Object& result = Object::Handle(isolate);

  if (number_of_arguments < 0) {
    return Api::NewError(
        "%s expects argument 'number_of_arguments' to be non-negative.",
        CURRENT_FUNC);
  }

  // Get the class to instantiate.
  Object& unchecked_type = Object::Handle(Api::UnwrapHandle(type));
  if (unchecked_type.IsNull() || !unchecked_type.IsType()) {
    RETURN_TYPE_ERROR(isolate, type, Type);
  }
  Type& type_obj = Type::Handle();
  type_obj ^= unchecked_type.raw();
  Class& cls = Class::Handle(isolate, type_obj.type_class());
  AbstractTypeArguments& type_arguments =
      AbstractTypeArguments::Handle(isolate, type_obj.arguments());

  const String& base_constructor_name = String::Handle(isolate, cls.Name());

  // And get the name of the constructor to invoke.
  String& dot_name = String::Handle(isolate);
  result = Api::UnwrapHandle(constructor_name);
  if (result.IsNull()) {
    dot_name = Symbols::Dot().raw();
  } else if (result.IsString()) {
    dot_name = String::Concat(Symbols::Dot(), String::Cast(result));
  } else {
    RETURN_TYPE_ERROR(isolate, constructor_name, String);
  }
  Dart_Handle state = Api::CheckIsolateState(isolate);
  if (::Dart_IsError(state)) {
    return state;
  }

  // Resolve the constructor.
  String& constr_name =
      String::Handle(String::Concat(base_constructor_name, dot_name));
  result = ResolveConstructor("Dart_New",
                              cls,
                              base_constructor_name,
                              constr_name,
                              number_of_arguments);
  if (result.IsError()) {
    return Api::NewHandle(isolate, result.raw());
  }
  ASSERT(result.IsFunction());
  Function& constructor = Function::Handle(isolate);
  constructor ^= result.raw();

  Instance& new_object = Instance::Handle(isolate);
  if (constructor.IsRedirectingFactory()) {
    ClassFinalizer::ResolveRedirectingFactory(cls, constructor);
    Type& redirect_type = Type::Handle(constructor.RedirectionType());
    constructor = constructor.RedirectionTarget();
    if (constructor.IsNull()) {
      ASSERT(redirect_type.IsMalformed());
      return Api::NewHandle(isolate, redirect_type.malformed_error());
    }

    if (!redirect_type.IsInstantiated()) {
      // The type arguments of the redirection type are instantiated from the
      // type arguments of the type argument.
      Error& malformed_error = Error::Handle();
      redirect_type ^= redirect_type.InstantiateFrom(type_arguments,
                                                     &malformed_error);
      if (!malformed_error.IsNull()) {
        return Api::NewHandle(isolate, malformed_error.raw());
      }
    }

    type_obj = redirect_type.raw();
    type_arguments = redirect_type.arguments();

    cls = type_obj.type_class();
  }
  if (constructor.IsConstructor()) {
    // Create the new object.
    new_object = Instance::New(cls);
  }

  // Create the argument list.
  intptr_t arg_index = 0;
  int extra_args = (constructor.IsConstructor() ? 2 : 1);
  const Array& args =
      Array::Handle(isolate, Array::New(number_of_arguments + extra_args));
  if (constructor.IsConstructor()) {
    // Constructors get the uninitialized object and a constructor phase.
    if (!type_arguments.IsNull()) {
      // The type arguments will be null if the class has no type parameters, in
      // which case the following call would fail because there is no slot
      // reserved in the object for the type vector.
      new_object.SetTypeArguments(type_arguments);
    }
    args.SetAt(arg_index++, new_object);
    args.SetAt(arg_index++,
               Smi::Handle(isolate, Smi::New(Function::kCtorPhaseAll)));
  } else {
    // Factories get type arguments.
    args.SetAt(arg_index++, type_arguments);
  }
  Object& argument = Object::Handle(isolate);
  for (int i = 0; i < number_of_arguments; i++) {
    argument = Api::UnwrapHandle(arguments[i]);
    if (!argument.IsNull() && !argument.IsInstance()) {
      if (argument.IsError()) {
        return Api::NewHandle(isolate, argument.raw());
      } else {
        return Api::NewError(
            "%s expects arguments[%d] to be an Instance handle.",
            CURRENT_FUNC, i);
      }
    }
    args.SetAt(arg_index++, argument);
  }

  // Invoke the constructor and return the new object.
  result = DartEntry::InvokeFunction(constructor, args);
  if (result.IsError()) {
    return Api::NewHandle(isolate, result.raw());
  }
  if (constructor.IsConstructor()) {
    ASSERT(result.IsNull());
  } else {
    ASSERT(result.IsNull() || result.IsInstance());
    new_object ^= result.raw();
  }
  return Api::NewHandle(isolate, new_object.raw());
}


DART_EXPORT Dart_Handle Dart_Allocate(Dart_Handle type) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);

  const Type& type_obj = Api::UnwrapTypeHandle(isolate, type);
  // Get the class to instantiate.
  if (type_obj.IsNull()) {
    RETURN_TYPE_ERROR(isolate, type, Type);
  }
  const Class& cls = Class::Handle(isolate, type_obj.type_class());

  // Allocate an object for the given class.
  return Api::NewHandle(isolate, Instance::New(cls));
}


static Dart_Handle SetupArguments(Isolate* isolate,
                                  int num_args,
                                  Dart_Handle* arguments,
                                  int extra_args,
                                  Array* args) {
  // Check for malformed arguments in the arguments list.
  *args = Array::New(num_args + extra_args);
  Object& arg = Object::Handle(isolate);
  for (int i = 0; i < num_args; i++) {
    arg = Api::UnwrapHandle(arguments[i]);
    if (!arg.IsNull() && !arg.IsInstance()) {
      *args = Array::null();
      if (arg.IsError()) {
        return Api::NewHandle(isolate, arg.raw());
      } else {
        return Api::NewError(
            "%s expects arguments[%d] to be an Instance handle.",
            "Dart_Invoke", i);
      }
    }
    args->SetAt((i + extra_args), arg);
  }
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_InvokeConstructor(Dart_Handle object,
                                               Dart_Handle name,
                                               int number_of_arguments,
                                               Dart_Handle* arguments) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);

  if (number_of_arguments < 0) {
    return Api::NewError(
        "%s expects argument 'number_of_arguments' to be non-negative.",
        CURRENT_FUNC);
  }
  const String& constructor_name = Api::UnwrapStringHandle(isolate, name);
  if (constructor_name.IsNull()) {
    RETURN_TYPE_ERROR(isolate, name, String);
  }
  const Instance& instance = Api::UnwrapInstanceHandle(isolate, object);
  if (instance.IsNull()) {
    RETURN_TYPE_ERROR(isolate, object, Instance);
  }

  // Since we have allocated an object it would mean that all classes
  // are finalized and hence it is not necessary to call
  // Api::CheckIsolateState.
  // TODO(asiva): How do we ensure that a constructor is not called more than
  // once for the same object.

  // Construct name of the constructor to invoke.
  const Type& type_obj = Type::Handle(isolate, instance.GetType());
  const Class& cls = Class::Handle(isolate, type_obj.type_class());
  const String& class_name = String::Handle(isolate, cls.Name());
  const Array& strings = Array::Handle(Array::New(3));
  strings.SetAt(0, class_name);
  strings.SetAt(1, Symbols::Dot());
  strings.SetAt(2, constructor_name);
  const String& dot_name = String::Handle(isolate, String::ConcatAll(strings));
  const AbstractTypeArguments& type_arguments =
    AbstractTypeArguments::Handle(isolate, type_obj.arguments());
  const Function& constructor =
    Function::Handle(isolate, cls.LookupFunctionAllowPrivate(dot_name));
  const int extra_args = 2;
  if (!constructor.IsNull() &&
      constructor.IsConstructor() &&
      constructor.AreValidArgumentCounts(number_of_arguments + extra_args,
                                         0,
                                         NULL)) {
    // Create the argument list.
    // Constructors get the uninitialized object and a constructor phase.
    if (!type_arguments.IsNull()) {
      // The type arguments will be null if the class has no type
      // parameters, in which case the following call would fail
      // because there is no slot reserved in the object for the
      // type vector.
      instance.SetTypeArguments(type_arguments);
    }
    Dart_Handle result;
    Array& args = Array::Handle(isolate);
    result = SetupArguments(isolate,
                            number_of_arguments,
                            arguments,
                            extra_args,
                            &args);
    if (!::Dart_IsError(result)) {
      args.SetAt(0, instance);
      args.SetAt(1, Smi::Handle(isolate, Smi::New(Function::kCtorPhaseAll)));
      const Object& retval = Object::Handle(
          isolate,
          DartEntry::InvokeFunction(constructor, args));
      if (retval.IsError()) {
        result = Api::NewHandle(isolate, retval.raw());
      } else {
        result = Api::NewHandle(isolate, instance.raw());
      }
    }
    return result;
  }
  return Api::NewError(
      "%s expects argument 'name' to be a valid constructor.",
      CURRENT_FUNC);
}


DART_EXPORT Dart_Handle Dart_Invoke(Dart_Handle target,
                                    Dart_Handle name,
                                    int number_of_arguments,
                                    Dart_Handle* arguments) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);

  const String& function_name = Api::UnwrapStringHandle(isolate, name);
  if (function_name.IsNull()) {
    RETURN_TYPE_ERROR(isolate, name, String);
  }
  if (number_of_arguments < 0) {
    return Api::NewError(
        "%s expects argument 'number_of_arguments' to be non-negative.",
        CURRENT_FUNC);
  }
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(target));
  if (obj.IsError()) {
    return target;
  }
  Dart_Handle result;
  Array& args = Array::Handle(isolate);
  if (obj.IsType()) {
    // Finalize all classes.
    Dart_Handle state = Api::CheckIsolateState(isolate);
    if (::Dart_IsError(state)) {
      return state;
    }

    const Class& cls = Class::Handle(isolate, Type::Cast(obj).type_class());
    const Function& function = Function::Handle(
        isolate,
        Resolver::ResolveStatic(cls,
                                function_name,
                                number_of_arguments,
                                Object::empty_array(),
                                Resolver::kIsQualified));
    if (function.IsNull()) {
      const String& cls_name = String::Handle(isolate, cls.Name());
      return Api::NewError("%s: did not find static method '%s.%s'.",
                           CURRENT_FUNC,
                           cls_name.ToCString(),
                           function_name.ToCString());
    }
    // Setup args and check for malformed arguments in the arguments list.
    result = SetupArguments(isolate, number_of_arguments, arguments, 0, &args);
    if (!::Dart_IsError(result)) {
      result = Api::NewHandle(isolate,
                              DartEntry::InvokeFunction(function, args));
    }
    return result;
  } else if (obj.IsNull() || obj.IsInstance()) {
    // Since we have allocated an object it would mean that all classes
    // are finalized and hence it is not necessary to call
    // Api::CheckIsolateState.
    Instance& instance = Instance::Handle(isolate);
    instance ^= obj.raw();
    ArgumentsDescriptor args_desc(
        Array::Handle(ArgumentsDescriptor::New(number_of_arguments + 1)));
    const Function& function = Function::Handle(
        isolate,
        Resolver::ResolveDynamic(instance, function_name, args_desc));
    if (function.IsNull()) {
      // Setup args and check for malformed arguments in the arguments list.
      result = SetupArguments(isolate,
                              number_of_arguments,
                              arguments,
                              1,
                              &args);
      if (!::Dart_IsError(result)) {
        args.SetAt(0, instance);
        const Array& args_descriptor =
          Array::Handle(ArgumentsDescriptor::New(args.Length()));
        result = Api::NewHandle(isolate,
                                DartEntry::InvokeNoSuchMethod(instance,
                                                              function_name,
                                                              args,
                                                              args_descriptor));
      }
      return result;
    }
    // Setup args and check for malformed arguments in the arguments list.
    result = SetupArguments(isolate, number_of_arguments, arguments, 1, &args);
    if (!::Dart_IsError(result)) {
      args.SetAt(0, instance);
      result = Api::NewHandle(isolate,
                              DartEntry::InvokeFunction(function, args));
    }
    return result;
  } else if (obj.IsLibrary()) {
    // Check whether class finalization is needed.
    const Library& lib = Library::Cast(obj);

    // Finalize all classes if needed.
    Dart_Handle state = Api::CheckIsolateState(isolate);
    if (::Dart_IsError(state)) {
      return state;
    }

    const Function& function =
        Function::Handle(isolate,
                         lib.LookupFunctionAllowPrivate(function_name));
    if (function.IsNull()) {
      return Api::NewError("%s: did not find top-level function '%s'.",
                           CURRENT_FUNC,
                           function_name.ToCString());
    }
    // LookupFunctionAllowPrivate does not check argument arity, so we
    // do it here.
    String& error_message = String::Handle();
    if (!function.AreValidArgumentCounts(number_of_arguments,
                                         0,
                                         &error_message)) {
      return Api::NewError("%s: wrong argument count for function '%s': %s.",
                           CURRENT_FUNC,
                           function_name.ToCString(),
                           error_message.ToCString());
    }
    // Setup args and check for malformed arguments in the arguments list.
    result = SetupArguments(isolate, number_of_arguments, arguments, 0, &args);
    if (!::Dart_IsError(result)) {
      result = Api::NewHandle(isolate,
                              DartEntry::InvokeFunction(function, args));
    }
    return result;
  } else {
    return Api::NewError(
        "%s expects argument 'target' to be an object, type, or library.",
        CURRENT_FUNC);
  }
}


DART_EXPORT Dart_Handle Dart_InvokeClosure(Dart_Handle closure,
                                           int number_of_arguments,
                                           Dart_Handle* arguments) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);
  const Instance& closure_obj = Api::UnwrapInstanceHandle(isolate, closure);
  if (closure_obj.IsNull() || !closure_obj.IsCallable(NULL, NULL)) {
    RETURN_TYPE_ERROR(isolate, closure, Instance);
  }
  if (number_of_arguments < 0) {
    return Api::NewError(
        "%s expects argument 'number_of_arguments' to be non-negative.",
        CURRENT_FUNC);
  }
  ASSERT(ClassFinalizer::AllClassesFinalized());

  // Set up arguments to include the closure as the first argument.
  const Array& args = Array::Handle(isolate,
                                    Array::New(number_of_arguments + 1));
  Object& obj = Object::Handle(isolate);
  args.SetAt(0, closure_obj);
  for (int i = 0; i < number_of_arguments; i++) {
    obj = Api::UnwrapHandle(arguments[i]);
    if (!obj.IsNull() && !obj.IsInstance()) {
      RETURN_TYPE_ERROR(isolate, arguments[i], Instance);
    }
    args.SetAt(i + 1, obj);
  }
  // Now try to invoke the closure.
  return Api::NewHandle(isolate, DartEntry::InvokeClosure(args));
}


DART_EXPORT Dart_Handle Dart_GetField(Dart_Handle container, Dart_Handle name) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);

  const String& field_name = Api::UnwrapStringHandle(isolate, name);
  if (field_name.IsNull()) {
    RETURN_TYPE_ERROR(isolate, name, String);
  }

  // Finalize all classes.
  Dart_Handle state = Api::CheckIsolateState(isolate);
  if (::Dart_IsError(state)) {
    return state;
  }

  Field& field = Field::Handle(isolate);
  Function& getter = Function::Handle(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(container));
  if (obj.IsNull()) {
    return Api::NewError("%s expects argument 'container' to be non-null.",
                         CURRENT_FUNC);
  } else if (obj.IsType()) {
    // To access a static field we may need to use the Field or the
    // getter Function.
    Class& cls = Class::Handle(isolate, Type::Cast(obj).type_class());

    field = cls.LookupStaticField(field_name);
    if (field.IsNull() || field.IsUninitialized()) {
      const String& getter_name =
          String::Handle(isolate, Field::GetterName(field_name));
      getter = cls.LookupStaticFunctionAllowPrivate(getter_name);
    }

    if (!getter.IsNull()) {
      // Invoke the getter and return the result.
      return Api::NewHandle(
          isolate, DartEntry::InvokeFunction(getter, Object::empty_array()));
    } else if (!field.IsNull()) {
      return Api::NewHandle(isolate, field.value());
    } else {
      return Api::NewError("%s: did not find static field '%s'.",
                           CURRENT_FUNC, field_name.ToCString());
    }

  } else if (obj.IsInstance()) {
    // Every instance field has a getter Function.  Try to find the
    // getter in any superclass and use that function to access the
    // field.
    const Instance& instance = Instance::Cast(obj);
    Class& cls = Class::Handle(isolate, instance.clazz());
    String& getter_name =
        String::Handle(isolate, Field::GetterName(field_name));
    while (!cls.IsNull()) {
      getter = cls.LookupDynamicFunctionAllowPrivate(getter_name);
      if (!getter.IsNull()) {
        break;
      }
      cls = cls.SuperClass();
    }

    // Invoke the getter and return the result.
    const int kNumArgs = 1;
    const Array& args = Array::Handle(isolate, Array::New(kNumArgs));
    args.SetAt(0, instance);
    if (getter.IsNull()) {
      const Array& args_descriptor =
          Array::Handle(ArgumentsDescriptor::New(args.Length()));
      return Api::NewHandle(isolate,
                            DartEntry::InvokeNoSuchMethod(instance,
                                                          getter_name,
                                                          args,
                                                          args_descriptor));
    }
    return Api::NewHandle(isolate, DartEntry::InvokeFunction(getter, args));

  } else if (obj.IsLibrary()) {
    // To access a top-level we may need to use the Field or the
    // getter Function.  The getter function may either be in the
    // library or in the field's owner class, depending.
    const Library& lib = Library::Cast(obj);
    field = lib.LookupFieldAllowPrivate(field_name);
    if (field.IsNull()) {
      // No field found and no ambiguity error.  Check for a getter in the lib.
      const String& getter_name =
          String::Handle(isolate, Field::GetterName(field_name));
      getter = lib.LookupFunctionAllowPrivate(getter_name);
    } else if (!field.IsNull() && field.IsUninitialized()) {
      // A field was found.  Check for a getter in the field's owner classs.
      const Class& cls = Class::Handle(isolate, field.owner());
      const String& getter_name =
          String::Handle(isolate, Field::GetterName(field_name));
      getter = cls.LookupStaticFunctionAllowPrivate(getter_name);
    }

    if (!getter.IsNull()) {
      // Invoke the getter and return the result.
      return Api::NewHandle(
          isolate, DartEntry::InvokeFunction(getter, Object::empty_array()));
    }
    if (!field.IsNull()) {
      return Api::NewHandle(isolate, field.value());
    }
    return Api::NewError("%s: did not find top-level variable '%s'.",
                         CURRENT_FUNC, field_name.ToCString());

  } else if (obj.IsError()) {
      return container;
  } else {
    return Api::NewError(
        "%s expects argument 'container' to be an object, type, or library.",
        CURRENT_FUNC);
  }
}


DART_EXPORT Dart_Handle Dart_SetField(Dart_Handle container,
                                      Dart_Handle name,
                                      Dart_Handle value) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  CHECK_CALLBACK_STATE(isolate);

  const String& field_name = Api::UnwrapStringHandle(isolate, name);
  if (field_name.IsNull()) {
    RETURN_TYPE_ERROR(isolate, name, String);
  }

  // Since null is allowed for value, we don't use UnwrapInstanceHandle.
  const Object& value_obj = Object::Handle(isolate, Api::UnwrapHandle(value));
  if (!value_obj.IsNull() && !value_obj.IsInstance()) {
    RETURN_TYPE_ERROR(isolate, value, Instance);
  }
  Instance& value_instance = Instance::Handle(isolate);
  value_instance ^= value_obj.raw();

  // Finalize all classes.
  Dart_Handle state = Api::CheckIsolateState(isolate);
  if (::Dart_IsError(state)) {
    return state;
  }
  Field& field = Field::Handle(isolate);
  Function& setter = Function::Handle(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(container));
  if (obj.IsNull()) {
    return Api::NewError("%s expects argument 'container' to be non-null.",
                         CURRENT_FUNC);
  } else if (obj.IsType()) {
    // To access a static field we may need to use the Field or the
    // setter Function.
    Class& cls = Class::Handle(isolate, Type::Cast(obj).type_class());

    field = cls.LookupStaticField(field_name);
    if (field.IsNull()) {
      String& setter_name =
          String::Handle(isolate, Field::SetterName(field_name));
      setter = cls.LookupStaticFunctionAllowPrivate(setter_name);
    }

    if (!setter.IsNull()) {
      // Invoke the setter and return the result.
      const int kNumArgs = 1;
      const Array& args = Array::Handle(isolate, Array::New(kNumArgs));
      args.SetAt(0, value_instance);
      const Object& result =
          Object::Handle(isolate, DartEntry::InvokeFunction(setter, args));
      if (result.IsError()) {
        return Api::NewHandle(isolate, result.raw());
      } else {
        return Api::Success();
      }
    } else if (!field.IsNull()) {
      if (field.is_final()) {
        return Api::NewError("%s: cannot set final field '%s'.",
                             CURRENT_FUNC, field_name.ToCString());
      } else {
        field.set_value(value_instance);
        return Api::Success();
      }
    } else {
      return Api::NewError("%s: did not find static field '%s'.",
                           CURRENT_FUNC, field_name.ToCString());
    }

  } else if (obj.IsInstance()) {
    // Every instance field has a setter Function.  Try to find the
    // setter in any superclass and use that function to access the
    // field.
    const Instance& instance = Instance::Cast(obj);
    Class& cls = Class::Handle(isolate, instance.clazz());
    String& setter_name =
        String::Handle(isolate, Field::SetterName(field_name));
    while (!cls.IsNull()) {
      field = cls.LookupInstanceField(field_name);
      if (!field.IsNull() && field.is_final()) {
        return Api::NewError("%s: cannot set final field '%s'.",
                             CURRENT_FUNC, field_name.ToCString());
      }
      setter = cls.LookupDynamicFunctionAllowPrivate(setter_name);
      if (!setter.IsNull()) {
        break;
      }
      cls = cls.SuperClass();
    }

    // Invoke the setter and return the result.
    const int kNumArgs = 2;
    const Array& args = Array::Handle(isolate, Array::New(kNumArgs));
    args.SetAt(0, instance);
    args.SetAt(1, value_instance);
    if (setter.IsNull()) {
      const Array& args_descriptor =
          Array::Handle(ArgumentsDescriptor::New(args.Length()));
      return Api::NewHandle(isolate,
                            DartEntry::InvokeNoSuchMethod(instance,
                                                          setter_name,
                                                          args,
                                                          args_descriptor));
    }
    return Api::NewHandle(isolate, DartEntry::InvokeFunction(setter, args));

  } else if (obj.IsLibrary()) {
    // To access a top-level we may need to use the Field or the
    // setter Function.  The setter function may either be in the
    // library or in the field's owner class, depending.
    const Library& lib = Library::Cast(obj);
    field = lib.LookupFieldAllowPrivate(field_name);
    if (field.IsNull()) {
      const String& setter_name =
          String::Handle(isolate, Field::SetterName(field_name));
      setter ^= lib.LookupFunctionAllowPrivate(setter_name);
    }

    if (!setter.IsNull()) {
      // Invoke the setter and return the result.
      const int kNumArgs = 1;
      const Array& args = Array::Handle(isolate, Array::New(kNumArgs));
      args.SetAt(0, value_instance);
      const Object& result =
          Object::Handle(isolate, DartEntry::InvokeFunction(setter, args));
      if (result.IsError()) {
        return Api::NewHandle(isolate, result.raw());
      }
      return Api::Success();
    }
    if (!field.IsNull()) {
      if (field.is_final()) {
        return Api::NewError("%s: cannot set final top-level variable '%s'.",
                             CURRENT_FUNC, field_name.ToCString());
      }
      field.set_value(value_instance);
      return Api::Success();
    }
    return Api::NewError("%s: did not find top-level variable '%s'.",
                         CURRENT_FUNC, field_name.ToCString());

  } else if (obj.IsError()) {
    return container;
  }
  return Api::NewError(
      "%s expects argument 'container' to be an object, type, or library.",
      CURRENT_FUNC);
}


// --- Exceptions ----

DART_EXPORT Dart_Handle Dart_ThrowException(Dart_Handle exception) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  CHECK_CALLBACK_STATE(isolate);
  {
    const Instance& excp = Api::UnwrapInstanceHandle(isolate, exception);
    if (excp.IsNull()) {
      RETURN_TYPE_ERROR(isolate, exception, Instance);
    }
  }
  if (isolate->top_exit_frame_info() == 0) {
    // There are no dart frames on the stack so it would be illegal to
    // throw an exception here.
    return Api::NewError("No Dart frames on stack, cannot throw exception");
  }

  // Unwind all the API scopes till the exit frame before throwing an
  // exception.
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  const Instance* saved_exception;
  {
    NoGCScope no_gc;
    RawInstance* raw_exception =
        Api::UnwrapInstanceHandle(isolate, exception).raw();
    state->UnwindScopes(isolate->top_exit_frame_info());
    saved_exception = &Instance::Handle(raw_exception);
  }
  Exceptions::Throw(*saved_exception);
  return Api::NewError("Exception was not thrown, internal error");
}


DART_EXPORT Dart_Handle Dart_ReThrowException(Dart_Handle exception,
                                              Dart_Handle stacktrace) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  CHECK_CALLBACK_STATE(isolate);
  {
    const Instance& excp = Api::UnwrapInstanceHandle(isolate, exception);
    if (excp.IsNull()) {
      RETURN_TYPE_ERROR(isolate, exception, Instance);
    }
    const Instance& stk = Api::UnwrapInstanceHandle(isolate, stacktrace);
    if (stk.IsNull()) {
      RETURN_TYPE_ERROR(isolate, stacktrace, Instance);
    }
  }
  if (isolate->top_exit_frame_info() == 0) {
    // There are no dart frames on the stack so it would be illegal to
    // throw an exception here.
    return Api::NewError("No Dart frames on stack, cannot throw exception");
  }

  // Unwind all the API scopes till the exit frame before throwing an
  // exception.
  ApiState* state = isolate->api_state();
  ASSERT(state != NULL);
  const Instance* saved_exception;
  const Instance* saved_stacktrace;
  {
    NoGCScope no_gc;
    RawInstance* raw_exception =
        Api::UnwrapInstanceHandle(isolate, exception).raw();
    RawInstance* raw_stacktrace =
        Api::UnwrapInstanceHandle(isolate, stacktrace).raw();
    state->UnwindScopes(isolate->top_exit_frame_info());
    saved_exception = &Instance::Handle(raw_exception);
    saved_stacktrace = &Instance::Handle(raw_stacktrace);
  }
  Exceptions::ReThrow(*saved_exception, *saved_stacktrace);
  return Api::NewError("Exception was not re thrown, internal error");
}


// --- Native fields and functions ---

DART_EXPORT Dart_Handle Dart_CreateNativeWrapperClass(Dart_Handle library,
                                                      Dart_Handle name,
                                                      int field_count) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const String& cls_name = Api::UnwrapStringHandle(isolate, name);
  if (cls_name.IsNull()) {
    RETURN_TYPE_ERROR(isolate, name, String);
  }
  const Library& lib = Api::UnwrapLibraryHandle(isolate, library);
  if (lib.IsNull()) {
    RETURN_TYPE_ERROR(isolate, library, Library);
  }
  if (!Utils::IsUint(16, field_count)) {
    return Api::NewError(
        "Invalid field_count passed to Dart_CreateNativeWrapperClass");
  }
  CHECK_CALLBACK_STATE(isolate);

  String& cls_symbol = String::Handle(isolate, Symbols::New(cls_name));
  const Class& cls = Class::Handle(
      isolate, Class::NewNativeWrapper(lib, cls_symbol, field_count));
  if (cls.IsNull()) {
    return Api::NewError(
        "Unable to create native wrapper class : already exists");
  }
  return Api::NewHandle(isolate, cls.RareType());
}


DART_EXPORT Dart_Handle Dart_GetNativeInstanceFieldCount(Dart_Handle obj,
                                                         int* count) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Instance& instance = Api::UnwrapInstanceHandle(isolate, obj);
  if (instance.IsNull()) {
    RETURN_TYPE_ERROR(isolate, obj, Instance);
  }
  const Class& cls = Class::Handle(isolate, instance.clazz());
  *count = cls.num_native_fields();
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_GetNativeInstanceField(Dart_Handle obj,
                                                    int index,
                                                    intptr_t* value) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Instance& instance = Api::UnwrapInstanceHandle(isolate, obj);
  if (instance.IsNull()) {
    RETURN_TYPE_ERROR(isolate, obj, Instance);
  }
  if (!instance.IsValidNativeIndex(index)) {
    return Api::NewError(
        "%s: invalid index %d passed in to access native instance field",
        CURRENT_FUNC, index);
  }
  *value = instance.GetNativeField(isolate, index);
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_SetNativeInstanceField(Dart_Handle obj,
                                                    int index,
                                                    intptr_t value) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Instance& instance = Api::UnwrapInstanceHandle(isolate, obj);
  if (instance.IsNull()) {
    RETURN_TYPE_ERROR(isolate, obj, Instance);
  }
  if (!instance.IsValidNativeIndex(index)) {
    return Api::NewError(
        "%s: invalid index %d passed in to set native instance field",
        CURRENT_FUNC, index);
  }
  instance.SetNativeField(index, value);
  return Api::Success();
}


DART_EXPORT void* Dart_GetNativeIsolateData(Dart_NativeArguments args) {
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  Isolate* isolate = arguments->isolate();
  ASSERT(isolate);
  return isolate->init_callback_data();
}


DART_EXPORT Dart_Handle Dart_GetNativeArgument(Dart_NativeArguments args,
                                               int index) {
  TRACE_API_CALL(CURRENT_FUNC);
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  if ((index < 0) || (index >= arguments->NativeArgCount())) {
    return Api::NewError(
        "%s: argument 'index' out of range. Expected 0..%d but saw %d.",
        CURRENT_FUNC, arguments->NativeArgCount() - 1, index);
  }
  return Api::NewHandle(arguments->isolate(), arguments->NativeArgAt(index));
}


DART_EXPORT int Dart_GetNativeArgumentCount(Dart_NativeArguments args) {
  TRACE_API_CALL(CURRENT_FUNC);
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  return arguments->NativeArgCount();
}


DART_EXPORT Dart_Handle Dart_GetNativeFieldOfArgument(Dart_NativeArguments args,
                                                      int arg_index,
                                                      int fld_index,
                                                      intptr_t* value) {
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  if ((arg_index < 0) || (arg_index >= arguments->NativeArgCount())) {
    return Api::NewError(
        "%s: argument 'arg_index' out of range. Expected 0..%d but saw %d.",
        CURRENT_FUNC, arguments->NativeArgCount() - 1, arg_index);
  }
  Isolate* isolate = arguments->isolate();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate,
                                     arguments->NativeArgAt(arg_index));
  if (!obj.IsInstance()) {
    return Api::NewError("%s expects argument at index '%d' to be of"
                         " type Instance.", CURRENT_FUNC, arg_index);
  }
  if (obj.IsNull()) {
    return Api::NewError("%s expects argument at index '%d' to be non-null.",
                         CURRENT_FUNC, arg_index);
  }
  const Instance& instance = Instance::Cast(obj);
  if (!instance.IsValidNativeIndex(fld_index)) {
    return Api::NewError(
        "%s: invalid index %d passed in to access native instance field",
        CURRENT_FUNC, fld_index);
  }
  if (value == NULL) {
    RETURN_NULL_ERROR(value);
  }
  *value = instance.GetNativeField(isolate, fld_index);
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_GetNativeReceiver(Dart_NativeArguments args,
                                               intptr_t* value) {
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  Isolate* isolate = arguments->isolate();
  CHECK_ISOLATE(isolate);
  ReusableObjectHandleScope reused_obj_handle(isolate);
  Object& obj = reused_obj_handle.Handle();
  obj = arguments->NativeArg0();
  intptr_t cid = obj.GetClassId();
  if (cid <= kNumPredefinedCids) {
    if (cid == kNullCid) {
      return Api::NewError("%s expects receiver argument to be non-null.",
                           CURRENT_FUNC);
    }
    return Api::NewError("%s expects receiver argument to be of"
                         " type Instance.", CURRENT_FUNC);
  }
  const Instance& instance = Instance::Cast(obj);
  ASSERT(instance.IsValidNativeIndex(0));
  if (value == NULL) {
    RETURN_NULL_ERROR(value);
  }
  *value = instance.GetNativeField(isolate, 0);
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_GetNativeStringArgument(Dart_NativeArguments args,
                                                     int arg_index,
                                                     void** peer) {
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  Isolate* isolate = arguments->isolate();
  CHECK_ISOLATE(isolate);
  if (Api::StringGetPeerHelper(args, arg_index, peer)) {
    return Api::Success();
  }
  *peer = NULL;
  ReusableObjectHandleScope reused_obj_handle(isolate);
  Object& obj = reused_obj_handle.Handle();
  obj = arguments->NativeArgAt(arg_index);
  if (RawObject::IsStringClassId(obj.GetClassId())) {
    return Api::NewHandle(isolate, obj.raw());
  }
  if (obj.IsNull()) {
    return Api::Null();
  }
  return Api::NewError("%s expects argument to be of"
                       " type String.", CURRENT_FUNC);
}


DART_EXPORT Dart_Handle Dart_GetNativeIntegerArgument(Dart_NativeArguments args,
                                                      int index,
                                                      int64_t* value) {
  TRACE_API_CALL(CURRENT_FUNC);
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  if ((index < 0) || (index >= arguments->NativeArgCount())) {
    return Api::NewError(
        "%s: argument 'index' out of range. Expected 0..%d but saw %d.",
        CURRENT_FUNC, arguments->NativeArgCount() - 1, index);
  }
  Isolate* isolate = arguments->isolate();
  ReusableObjectHandleScope reused_obj_handle(isolate);
  Object& obj = reused_obj_handle.Handle();
  obj = arguments->NativeArgAt(index);
  intptr_t cid = obj.GetClassId();
  if (cid == kSmiCid) {
    *value = Smi::Cast(obj).Value();
    return Api::Success();
  }
  if (cid == kMintCid) {
    *value = Mint::Cast(obj).value();
    return Api::Success();
  }
  if (cid == kBigintCid) {
    const Bigint& bigint = Bigint::Cast(obj);
    if (BigintOperations::FitsIntoInt64(bigint)) {
      *value = BigintOperations::ToInt64(bigint);
      return Api::Success();
    }
    return Api::NewError(
        "%s: argument %d is a big integer that does not fit in 'value'.",
        CURRENT_FUNC, index);
  }
  return Api::NewError(
      "%s: argument %d is not an Integer argument.",
      CURRENT_FUNC, index);
}


DART_EXPORT Dart_Handle Dart_GetNativeBooleanArgument(Dart_NativeArguments args,
                                                      int index,
                                                      bool* value) {
  TRACE_API_CALL(CURRENT_FUNC);
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  if ((index < 0) || (index >= arguments->NativeArgCount())) {
    return Api::NewError(
        "%s: argument 'index' out of range. Expected 0..%d but saw %d.",
        CURRENT_FUNC, arguments->NativeArgCount() - 1, index);
  }
  Isolate* isolate = arguments->isolate();
  ReusableObjectHandleScope reused_obj_handle(isolate);
  Object& obj = reused_obj_handle.Handle();
  obj = arguments->NativeArgAt(index);
  intptr_t cid = obj.GetClassId();
  if (cid == kBoolCid) {
    *value = Bool::Cast(obj).value();
    return Api::Success();
  }
  if (obj.IsNull()) {
    *value = false;
    return Api::Success();
  }
  return Api::NewError(
      "%s: argument %d is not a Boolean argument.",
      CURRENT_FUNC, index);
}


DART_EXPORT Dart_Handle Dart_GetNativeDoubleArgument(Dart_NativeArguments args,
                                                     int index,
                                                     double* value) {
  TRACE_API_CALL(CURRENT_FUNC);
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  if ((index < 0) || (index >= arguments->NativeArgCount())) {
    return Api::NewError(
        "%s: argument 'index' out of range. Expected 0..%d but saw %d.",
        CURRENT_FUNC, arguments->NativeArgCount() - 1, index);
  }
  Isolate* isolate = arguments->isolate();
  ReusableObjectHandleScope reused_obj_handle(isolate);
  Object& obj = reused_obj_handle.Handle();
  obj = arguments->NativeArgAt(index);
  intptr_t cid = obj.GetClassId();
  if (cid == kDoubleCid) {
    *value = Double::Cast(obj).value();
    return Api::Success();
  }
  if (cid == kSmiCid) {
    *value = Smi::Cast(obj).AsDoubleValue();
    return Api::Success();
  }
  if (cid == kMintCid) {
    *value = Mint::Cast(obj).AsDoubleValue();
    return Api::Success();
  }
  if (cid == kBigintCid) {
    *value = Bigint::Cast(obj).AsDoubleValue();
    return Api::Success();
  }
  return Api::NewError(
      "%s: argument %d is not a Double argument.",
      CURRENT_FUNC, index);
}


DART_EXPORT void Dart_SetReturnValue(Dart_NativeArguments args,
                                     Dart_Handle retval) {
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  Isolate* isolate = arguments->isolate();
  CHECK_ISOLATE(isolate);
  if ((retval != Api::Null()) && (!Api::IsInstance(retval))) {
    const Object& ret_obj = Object::Handle(Api::UnwrapHandle(retval));
    FATAL1("Return value check failed: saw '%s' expected a dart Instance.",
           ret_obj.ToCString());
  }
  ASSERT(retval != 0);
  Api::SetReturnValue(arguments, retval);
}


DART_EXPORT void Dart_SetWeakHandleReturnValue(Dart_NativeArguments args,
                                               Dart_WeakPersistentHandle rval) {
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  Isolate* isolate = arguments->isolate();
  CHECK_ISOLATE(isolate);
  ASSERT(isolate->api_state() != NULL &&
         (isolate->api_state()->IsValidWeakPersistentHandle(rval) ||
          isolate->api_state()->IsValidPrologueWeakPersistentHandle(rval)));
  Api::SetWeakHandleReturnValue(arguments, rval);
}


DART_EXPORT void Dart_SetBooleanReturnValue(Dart_NativeArguments args,
                                            bool retval) {
  TRACE_API_CALL(CURRENT_FUNC);
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  arguments->SetReturn(Bool::Get(retval));
}


DART_EXPORT void Dart_SetIntegerReturnValue(Dart_NativeArguments args,
                                            int64_t retval) {
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  Isolate* isolate = arguments->isolate();
  CHECK_ISOLATE(isolate);
  if (Smi::IsValid64(retval)) {
    Api::SetSmiReturnValue(arguments, retval);
  } else {
    // Slow path for Mints and Bigints.
    ASSERT_CALLBACK_STATE(isolate);
    Api::SetIntegerReturnValue(arguments, retval);
  }
}


DART_EXPORT void Dart_SetDoubleReturnValue(Dart_NativeArguments args,
                                           double retval) {
  NativeArguments* arguments = reinterpret_cast<NativeArguments*>(args);
  Isolate* isolate = arguments->isolate();
  CHECK_ISOLATE(isolate);
  ASSERT_CALLBACK_STATE(isolate);
  Api::SetDoubleReturnValue(arguments, retval);
}


// --- Scripts and Libraries ---

DART_EXPORT Dart_Handle Dart_SetLibraryTagHandler(
    Dart_LibraryTagHandler handler) {
  Isolate* isolate = Isolate::Current();
  CHECK_ISOLATE(isolate);
  isolate->set_library_tag_handler(handler);
  return Api::Success();
}


// NOTE: Need to pass 'result' as a parameter here in order to avoid
// warning: variable 'result' might be clobbered by 'longjmp' or 'vfork'
// which shows up because of the use of setjmp.
static void CompileSource(Isolate* isolate,
                          const Library& lib,
                          const Script& script,
                          Dart_Handle* result) {
  bool update_lib_status = (script.kind() == RawScript::kScriptTag ||
                            script.kind() == RawScript::kLibraryTag);
  if (update_lib_status) {
    lib.SetLoadInProgress();
  }
  ASSERT(isolate != NULL);
  const Error& error = Error::Handle(isolate, Compiler::Compile(lib, script));
  if (error.IsNull()) {
    *result = Api::NewHandle(isolate, lib.raw());
    if (update_lib_status) {
      lib.SetLoaded();
    }
  } else {
    *result = Api::NewHandle(isolate, error.raw());
    if (update_lib_status) {
      lib.SetLoadError();
    }
  }
}


DART_EXPORT Dart_Handle Dart_LoadScript(Dart_Handle url,
                                        Dart_Handle source,
                                        intptr_t line_offset,
                                        intptr_t col_offset) {
  TIMERSCOPE(time_script_loading);
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const String& url_str = Api::UnwrapStringHandle(isolate, url);
  if (url_str.IsNull()) {
    RETURN_TYPE_ERROR(isolate, url, String);
  }
  const String& source_str = Api::UnwrapStringHandle(isolate, source);
  if (source_str.IsNull()) {
    RETURN_TYPE_ERROR(isolate, source, String);
  }
  Library& library =
      Library::Handle(isolate, isolate->object_store()->root_library());
  if (!library.IsNull()) {
    const String& library_url = String::Handle(isolate, library.url());
    return Api::NewError("%s: A script has already been loaded from '%s'.",
                         CURRENT_FUNC, library_url.ToCString());
  }
  if (line_offset < 0) {
    return Api::NewError("%s: argument 'line_offset' must be positive number",
                         CURRENT_FUNC);
  }
  if (col_offset < 0) {
    return Api::NewError("%s: argument 'col_offset' must be positive number",
                         CURRENT_FUNC);
  }
  CHECK_CALLBACK_STATE(isolate);

  library = Library::New(url_str);
  library.set_debuggable(true);
  library.Register();
  isolate->object_store()->set_root_library(library);

  const Script& script = Script::Handle(
      isolate, Script::New(url_str, source_str, RawScript::kScriptTag));
  script.SetLocationOffset(line_offset, col_offset);
  Dart_Handle result;
  CompileSource(isolate, library, script, &result);
  return result;
}


DART_EXPORT Dart_Handle Dart_LoadScriptFromSnapshot(const uint8_t* buffer,
                                                    intptr_t buffer_len) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  TIMERSCOPE(time_script_loading);
  if (buffer == NULL) {
    RETURN_NULL_ERROR(buffer);
  }
  NoHeapGrowthControlScope no_growth_control;

  const Snapshot* snapshot = Snapshot::SetupFromBuffer(buffer);
  if (!snapshot->IsScriptSnapshot()) {
    return Api::NewError("%s expects parameter 'buffer' to be a script type"
                         " snapshot.", CURRENT_FUNC);
  }
  if (snapshot->length() != buffer_len) {
    return Api::NewError("%s: 'buffer_len' of %" Pd " is not equal to %d which"
                         " is the expected length in the snapshot.",
                         CURRENT_FUNC, buffer_len, snapshot->length());
  }
  Library& library =
      Library::Handle(isolate, isolate->object_store()->root_library());
  if (!library.IsNull()) {
    const String& library_url = String::Handle(isolate, library.url());
    return Api::NewError("%s: A script has already been loaded from '%s'.",
                         CURRENT_FUNC, library_url.ToCString());
  }
  CHECK_CALLBACK_STATE(isolate);

  SnapshotReader reader(snapshot->content(),
                        snapshot->length(),
                        snapshot->kind(),
                        isolate);
  const Object& tmp = Object::Handle(isolate, reader.ReadObject());
  if (!tmp.IsLibrary()) {
    return Api::NewError("%s: Unable to deserialize snapshot correctly.",
                         CURRENT_FUNC);
  }
  library ^= tmp.raw();
  library.set_debuggable(true);
  isolate->object_store()->set_root_library(library);
  return Api::NewHandle(isolate, library.raw());
}


DART_EXPORT Dart_Handle Dart_RootLibrary() {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  Library& library =
      Library::Handle(isolate, isolate->object_store()->root_library());
  return Api::NewHandle(isolate, library.raw());
}


DART_EXPORT Dart_Handle Dart_GetClass(Dart_Handle library,
                                      Dart_Handle class_name) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Library& lib = Api::UnwrapLibraryHandle(isolate, library);
  if (lib.IsNull()) {
    RETURN_TYPE_ERROR(isolate, library, Library);
  }
  const String& cls_name = Api::UnwrapStringHandle(isolate, class_name);
  if (cls_name.IsNull()) {
    RETURN_TYPE_ERROR(isolate, class_name, String);
  }
  const Class& cls = Class::Handle(
      isolate, lib.LookupClassAllowPrivate(cls_name));
  if (cls.IsNull()) {
    // TODO(turnidge): Return null or error in this case?
    const String& lib_name = String::Handle(isolate, lib.name());
    return Api::NewError("Class '%s' not found in library '%s'.",
                         cls_name.ToCString(), lib_name.ToCString());
  }
  return Api::NewHandle(isolate, cls.RareType());
}


DART_EXPORT Dart_Handle Dart_GetType(Dart_Handle library,
                                     Dart_Handle class_name,
                                     intptr_t number_of_type_arguments,
                                     Dart_Handle* type_arguments) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);

  // Validate the input arguments.
  const Library& lib = Api::UnwrapLibraryHandle(isolate, library);
  if (lib.IsNull()) {
    RETURN_TYPE_ERROR(isolate, library, Library);
  }
  const String& name_str = Api::UnwrapStringHandle(isolate, class_name);
  if (name_str.IsNull()) {
    RETURN_TYPE_ERROR(isolate, class_name, String);
  }
  // Ensure all classes are finalized.
  Dart_Handle state = Api::CheckIsolateState(isolate);
  if (::Dart_IsError(state)) {
    return state;
  }
  const Class& cls =
      Class::Handle(isolate, lib.LookupClassAllowPrivate(name_str));
  if (cls.IsNull()) {
    const String& lib_name = String::Handle(isolate, lib.name());
    return Api::NewError("Type '%s' not found in library '%s'.",
                         name_str.ToCString(), lib_name.ToCString());
  }
  if (cls.NumTypeArguments() == 0) {
    if (number_of_type_arguments != 0) {
      return Api::NewError("Invalid number of type arguments specified, "
                           "got %" Pd " expected 0", number_of_type_arguments);
    }
    return Api::NewHandle(isolate, Type::NewNonParameterizedType(cls));
  }
  intptr_t num_expected_type_arguments = cls.NumTypeParameters();
  TypeArguments& type_args_obj = TypeArguments::Handle();
  if (number_of_type_arguments > 0) {
    if (type_arguments == NULL) {
      RETURN_NULL_ERROR(type_arguments);
    }
    if (num_expected_type_arguments != number_of_type_arguments) {
      return Api::NewError("Invalid number of type arguments specified, "
                           "got %" Pd " expected %" Pd,
                           number_of_type_arguments,
                           num_expected_type_arguments);
    }
    const Array& array = Api::UnwrapArrayHandle(isolate, *type_arguments);
    if (array.IsNull()) {
      RETURN_TYPE_ERROR(isolate, *type_arguments, Array);
    }
    if (array.Length() != num_expected_type_arguments) {
      return Api::NewError("Invalid type arguments specified, expected an "
                           "array of len %" Pd " but got an array of len %" Pd,
                           number_of_type_arguments,
                           array.Length());
    }
    // Set up the type arguments array.
    type_args_obj ^= TypeArguments::New(num_expected_type_arguments);
    AbstractType& type_arg = AbstractType::Handle();
    for (intptr_t i = 0; i < number_of_type_arguments; i++) {
      type_arg ^= array.At(i);
      type_args_obj.SetTypeAt(i, type_arg);
    }
  }

  // Construct the type object, canonicalize it and return.
  Type& instantiated_type = Type::Handle(
      Type::New(cls, type_args_obj, Scanner::kDummyTokenIndex));
  instantiated_type ^= ClassFinalizer::FinalizeType(
      cls, instantiated_type, ClassFinalizer::kCanonicalize);
  return Api::NewHandle(isolate, instantiated_type.raw());
}


DART_EXPORT Dart_Handle Dart_LibraryUrl(Dart_Handle library) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Library& lib = Api::UnwrapLibraryHandle(isolate, library);
  if (lib.IsNull()) {
    RETURN_TYPE_ERROR(isolate, library, Library);
  }
  const String& url = String::Handle(isolate, lib.url());
  ASSERT(!url.IsNull());
  return Api::NewHandle(isolate, url.raw());
}


DART_EXPORT Dart_Handle Dart_LookupLibrary(Dart_Handle url) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const String& url_str = Api::UnwrapStringHandle(isolate, url);
  if (url_str.IsNull()) {
    RETURN_TYPE_ERROR(isolate, url, String);
  }
  const Library& library =
      Library::Handle(isolate, Library::LookupLibrary(url_str));
  if (library.IsNull()) {
    return Api::NewError("%s: library '%s' not found.",
                         CURRENT_FUNC, url_str.ToCString());
  } else {
    return Api::NewHandle(isolate, library.raw());
  }
}


DART_EXPORT Dart_Handle Dart_LoadLibrary(Dart_Handle url,
                                         Dart_Handle source) {
  TIMERSCOPE(time_script_loading);
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const String& url_str = Api::UnwrapStringHandle(isolate, url);
  if (url_str.IsNull()) {
    RETURN_TYPE_ERROR(isolate, url, String);
  }
  const String& source_str = Api::UnwrapStringHandle(isolate, source);
  if (source_str.IsNull()) {
    RETURN_TYPE_ERROR(isolate, source, String);
  }
  CHECK_CALLBACK_STATE(isolate);

  Library& library = Library::Handle(isolate, Library::LookupLibrary(url_str));
  if (library.IsNull()) {
    library = Library::New(url_str);
    library.Register();
  } else if (!library.LoadNotStarted()) {
    // The source for this library has either been loaded or is in the
    // process of loading.  Return an error.
    return Api::NewError("%s: library '%s' has already been loaded.",
                         CURRENT_FUNC, url_str.ToCString());
  }
  const Script& script = Script::Handle(
      isolate, Script::New(url_str, source_str, RawScript::kLibraryTag));
  Dart_Handle result;
  CompileSource(isolate, library, script, &result);
  // Propagate the error out right now.
  if (::Dart_IsError(result)) {
    return result;
  }

  // If this is the dart:builtin library, register it with the VM.
  if (url_str.Equals("dart:builtin")) {
    isolate->object_store()->set_builtin_library(library);
    Dart_Handle state = Api::CheckIsolateState(isolate);
    if (::Dart_IsError(state)) {
      return state;
    }
  }
  return result;
}


DART_EXPORT Dart_Handle Dart_LibraryImportLibrary(Dart_Handle library,
                                                  Dart_Handle import,
                                                  Dart_Handle prefix) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Library& library_vm = Api::UnwrapLibraryHandle(isolate, library);
  if (library_vm.IsNull()) {
    RETURN_TYPE_ERROR(isolate, library, Library);
  }
  const Library& import_vm = Api::UnwrapLibraryHandle(isolate, import);
  if (import_vm.IsNull()) {
    RETURN_TYPE_ERROR(isolate, import, Library);
  }
  const Object& prefix_object =
      Object::Handle(isolate, Api::UnwrapHandle(prefix));
  const String& prefix_vm = prefix_object.IsNull()
      ? Symbols::Empty()
      : String::Cast(prefix_object);
  if (prefix_vm.IsNull()) {
    RETURN_TYPE_ERROR(isolate, prefix, String);
  }
  CHECK_CALLBACK_STATE(isolate);

  const String& prefix_symbol =
      String::Handle(isolate, Symbols::New(prefix_vm));
  const Namespace& import_ns = Namespace::Handle(
      Namespace::New(import_vm, Object::null_array(), Object::null_array()));
  if (prefix_vm.Length() == 0) {
    library_vm.AddImport(import_ns);
  } else {
    LibraryPrefix& library_prefix = LibraryPrefix::Handle();
    library_prefix = library_vm.LookupLocalLibraryPrefix(prefix_symbol);
    if (!library_prefix.IsNull()) {
      library_prefix.AddImport(import_ns);
    } else {
      library_prefix = LibraryPrefix::New(prefix_symbol, import_ns);
      library_vm.AddObject(library_prefix, prefix_symbol);
    }
  }
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_LoadSource(Dart_Handle library,
                                        Dart_Handle url,
                                        Dart_Handle source) {
  TIMERSCOPE(time_script_loading);
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Library& lib = Api::UnwrapLibraryHandle(isolate, library);
  if (lib.IsNull()) {
    RETURN_TYPE_ERROR(isolate, library, Library);
  }
  const String& url_str = Api::UnwrapStringHandle(isolate, url);
  if (url_str.IsNull()) {
    RETURN_TYPE_ERROR(isolate, url, String);
  }
  const String& source_str = Api::UnwrapStringHandle(isolate, source);
  if (source_str.IsNull()) {
    RETURN_TYPE_ERROR(isolate, source, String);
  }
  CHECK_CALLBACK_STATE(isolate);

  const Script& script = Script::Handle(
      isolate, Script::New(url_str, source_str, RawScript::kSourceTag));
  Dart_Handle result;
  CompileSource(isolate, lib, script, &result);
  return result;
}


DART_EXPORT Dart_Handle Dart_LibraryLoadPatch(Dart_Handle library,
                                              Dart_Handle url,
                                              Dart_Handle patch_source) {
  TIMERSCOPE(time_script_loading);
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Library& lib = Api::UnwrapLibraryHandle(isolate, library);
  if (lib.IsNull()) {
    RETURN_TYPE_ERROR(isolate, library, Library);
  }
  const String& url_str = Api::UnwrapStringHandle(isolate, url);
  if (url_str.IsNull()) {
    RETURN_TYPE_ERROR(isolate, url, String);
  }
  const String& source_str = Api::UnwrapStringHandle(isolate, patch_source);
  if (source_str.IsNull()) {
    RETURN_TYPE_ERROR(isolate, patch_source, String);
  }
  CHECK_CALLBACK_STATE(isolate);

  const Script& script = Script::Handle(
      isolate, Script::New(url_str, source_str, RawScript::kPatchTag));
  Dart_Handle result;
  CompileSource(isolate, lib, script, &result);
  return result;
}


DART_EXPORT Dart_Handle Dart_SetNativeResolver(
    Dart_Handle library,
    Dart_NativeEntryResolver resolver) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Library& lib = Api::UnwrapLibraryHandle(isolate, library);
  if (lib.IsNull()) {
    RETURN_TYPE_ERROR(isolate, library, Library);
  }
  lib.set_native_entry_resolver(resolver);
  return Api::Success();
}


// --- Peer support ---

DART_EXPORT Dart_Handle Dart_GetPeer(Dart_Handle object, void** peer) {
  if (peer == NULL) {
    RETURN_NULL_ERROR(peer);
  }
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(object));
  if (obj.IsNull() || obj.IsNumber() || obj.IsBool()) {
    const char* msg =
        "%s: argument 'object' cannot be a subtype of Null, num, or bool";
    return Api::NewError(msg, CURRENT_FUNC);
  }
  {
    NoGCScope no_gc;
    RawObject* raw_obj = obj.raw();
    *peer = isolate->heap()->GetPeer(raw_obj);
  }
  return Api::Success();
}


DART_EXPORT Dart_Handle Dart_SetPeer(Dart_Handle object, void* peer) {
  Isolate* isolate = Isolate::Current();
  DARTSCOPE(isolate);
  const Object& obj = Object::Handle(isolate, Api::UnwrapHandle(object));
  if (obj.IsNull() || obj.IsNumber() || obj.IsBool()) {
    const char* msg =
        "%s: argument 'object' cannot be a subtype of Null, num, or bool";
    return Api::NewError(msg, CURRENT_FUNC);
  }
  {
    NoGCScope no_gc;
    RawObject* raw_obj = obj.raw();
    isolate->heap()->SetPeer(raw_obj, peer);
  }
  return Api::Success();
}

}  // namespace dart

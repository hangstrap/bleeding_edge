// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_ARM.
#if defined(TARGET_ARCH_ARM)

#include "vm/intrinsifier.h"

#include "vm/assembler.h"
#include "vm/flow_graph_compiler.h"
#include "vm/object.h"
#include "vm/object_store.h"
#include "vm/symbols.h"

namespace dart {

DECLARE_FLAG(bool, enable_type_checks);


#define __ assembler->

void Intrinsifier::List_Allocate(Assembler* assembler) {
  const intptr_t kTypeArgumentsOffset = 1 * kWordSize;
  const intptr_t kArrayLengthOffset = 0 * kWordSize;
  Label fall_through;

  // Compute the size to be allocated, it is based on the array length
  // and is computed as:
  // RoundedAllocationSize((array_length * kwordSize) + sizeof(RawArray)).
  __ ldr(R3, Address(SP, kArrayLengthOffset));  // Array length.

  // Check that length is a positive Smi.
  __ tst(R3, ShifterOperand(kSmiTagMask));
  __ b(&fall_through, NE);
  __ cmp(R3, ShifterOperand(0));
  __ b(&fall_through, LT);

  // Check for maximum allowed length.
  const intptr_t max_len =
      reinterpret_cast<int32_t>(Smi::New(Array::kMaxElements));
  __ CompareImmediate(R3, max_len);
  __ b(&fall_through, GT);

  const intptr_t fixed_size = sizeof(RawArray) + kObjectAlignment - 1;
  __ LoadImmediate(R2, fixed_size);
  __ add(R2, R2, ShifterOperand(R3, LSL, 1));  // R3 is  a Smi.
  ASSERT(kSmiTagShift == 1);
  __ bic(R2, R2, ShifterOperand(kObjectAlignment - 1));

  // R2: Allocation size.

  Isolate* isolate = Isolate::Current();
  Heap* heap = isolate->heap();

  __ LoadImmediate(R6, heap->TopAddress());
  __ ldr(R0, Address(R6, 0));  // Potential new object start.
  __ adds(R1, R0, ShifterOperand(R2));  // Potential next object start.
  __ b(&fall_through, VS);

  // Check if the allocation fits into the remaining space.
  // R0: potential new object start.
  // R1: potential next object start.
  // R2: allocation size.
  __ LoadImmediate(R3, heap->EndAddress());
  __ ldr(R3, Address(R3, 0));
  __ cmp(R1, ShifterOperand(R3));
  __ b(&fall_through, CS);

  // Successfully allocated the object(s), now update top to point to
  // next object start and initialize the object.
  __ str(R1, Address(R6, 0));
  __ add(R0, R0, ShifterOperand(kHeapObjectTag));

  // Initialize the tags.
  // R0: new object start as a tagged pointer.
  // R1: new object end address.
  // R2: allocation size.
  {
    const intptr_t shift = RawObject::kSizeTagBit - kObjectAlignmentLog2;
    const Class& cls = Class::Handle(isolate->object_store()->array_class());

    __ CompareImmediate(R2, RawObject::SizeTag::kMaxSizeTag);
    __ mov(R2, ShifterOperand(R2, LSL, shift), LS);
    __ mov(R2, ShifterOperand(0), HI);

    // Get the class index and insert it into the tags.
    // R2: size and bit tags.
    __ LoadImmediate(TMP, RawObject::ClassIdTag::encode(cls.id()));
    __ orr(R2, R2, ShifterOperand(TMP));
    __ str(R2, FieldAddress(R0, Array::tags_offset()));  // Store tags.
  }

  // R0: new object start as a tagged pointer.
  // R1: new object end address.
  // Store the type argument field.
  __ ldr(R2, Address(SP, kTypeArgumentsOffset));  // Type argument.
  __ StoreIntoObjectNoBarrier(R0,
                              FieldAddress(R0, Array::type_arguments_offset()),
                              R2);

  // Set the length field.
  __ ldr(R2, Address(SP, kArrayLengthOffset));  // Array Length.
  __ StoreIntoObjectNoBarrier(R0,
                              FieldAddress(R0, Array::length_offset()),
                              R2);

  // Initialize all array elements to raw_null.
  // R0: new object start as a tagged pointer.
  // R1: new object end address.
  // R2: iterator which initially points to the start of the variable
  // data area to be initialized.
  // R3: null
  __ LoadImmediate(R3, reinterpret_cast<intptr_t>(Object::null()));
  __ AddImmediate(R2, R0, sizeof(RawArray) - kHeapObjectTag);

  Label init_loop;
  __ Bind(&init_loop);
  __ cmp(R2, ShifterOperand(R1));
  __ str(R3, Address(R2, 0), CC);
  __ AddImmediate(R2, kWordSize, CC);
  __ b(&init_loop, CC);

  __ Ret();  // Returns the newly allocated object in R0.
  __ Bind(&fall_through);
}


void Intrinsifier::Array_getLength(Assembler* assembler) {
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ ldr(R0, FieldAddress(R0, Array::length_offset()));
  __ Ret();
}


void Intrinsifier::ImmutableList_getLength(Assembler* assembler) {
  return Array_getLength(assembler);
}


void Intrinsifier::Array_getIndexed(Assembler* assembler) {
  Label fall_through;

  __ ldr(R0, Address(SP, + 0 * kWordSize));  // Index
  __ ldr(R1, Address(SP, + 1 * kWordSize));  // Array

  __ tst(R0, ShifterOperand(kSmiTagMask));
  __ b(&fall_through, NE);  // Index is not an smi, fall through

  // range check
  __ ldr(R6, FieldAddress(R1, Array::length_offset()));
  __ cmp(R0, ShifterOperand(R6));

  ASSERT(kSmiTagShift == 1);
  // array element at R1 + R0*2 + Array::data_offset - 1
  __ add(R6, R1, ShifterOperand(R0, LSL, 1), CC);
  __ ldr(R0, FieldAddress(R6, Array::data_offset()), CC);
  __ bx(LR, CC);
  __ Bind(&fall_through);
}


void Intrinsifier::ImmutableList_getIndexed(Assembler* assembler) {
  return Array_getIndexed(assembler);
}


static intptr_t ComputeObjectArrayTypeArgumentsOffset() {
  const Library& core_lib = Library::Handle(Library::CoreLibrary());
  const Class& cls = Class::Handle(
      core_lib.LookupClassAllowPrivate(Symbols::_List()));
  ASSERT(!cls.IsNull());
  ASSERT(cls.NumTypeArguments() == 1);
  const intptr_t field_offset = cls.type_arguments_field_offset();
  ASSERT(field_offset != Class::kNoTypeArguments);
  return field_offset;
}


// Intrinsify only for Smi value and index. Non-smi values need a store buffer
// update. Array length is always a Smi.
void Intrinsifier::Array_setIndexed(Assembler* assembler) {
  Label fall_through;

  if (FLAG_enable_type_checks) {
    const intptr_t type_args_field_offset =
        ComputeObjectArrayTypeArgumentsOffset();
    // Inline simple tests (Smi, null), fallthrough if not positive.
    const int32_t raw_null = reinterpret_cast<intptr_t>(Object::null());
    Label checked_ok;
    __ ldr(R2, Address(SP, 0 * kWordSize));  // Value.

    // Null value is valid for any type.
    __ CompareImmediate(R2, raw_null);
    __ b(&checked_ok, EQ);

    __ ldr(R1, Address(SP, 2 * kWordSize));  // Array.
    __ ldr(R1, FieldAddress(R1, type_args_field_offset));

    // R1: Type arguments of array.
    __ CompareImmediate(R1, raw_null);
    __ b(&checked_ok, EQ);

    // Check if it's dynamic.
    // For now handle only TypeArguments and bail out if InstantiatedTypeArgs.
    __ CompareClassId(R1, kTypeArgumentsCid, R0);
    __ b(&fall_through, NE);
    // Get type at index 0.
    __ ldr(R0, FieldAddress(R1, TypeArguments::type_at_offset(0)));
    __ CompareObject(R0, Type::ZoneHandle(Type::DynamicType()));
    __ b(&checked_ok, EQ);

    // Check for int and num.
    __ tst(R2, ShifterOperand(kSmiTagMask));  // Value is Smi?
    __ b(&fall_through, NE);  // Non-smi value.
    __ CompareObject(R0, Type::ZoneHandle(Type::IntType()));
    __ b(&checked_ok, EQ);
    __ CompareObject(R0, Type::ZoneHandle(Type::Number()));
    __ b(&fall_through, NE);
    __ Bind(&checked_ok);
  }
  __ ldr(R1, Address(SP, 1 * kWordSize));  // Index.
  __ tst(R1, ShifterOperand(kSmiTagMask));
  // Index not Smi.
  __ b(&fall_through, NE);
  __ ldr(R0, Address(SP, 2 * kWordSize));  // Array.

  // Range check.
  __ ldr(R3, FieldAddress(R0, Array::length_offset()));  // Array length.
  __ cmp(R1, ShifterOperand(R3));
  // Runtime throws exception.
  __ b(&fall_through, CS);

  // Note that R1 is Smi, i.e, times 2.
  ASSERT(kSmiTagShift == 1);
  __ ldr(R2, Address(SP, 0 * kWordSize));  // Value.
  __ add(R1, R0, ShifterOperand(R1, LSL, 1));  // R1 is Smi.
  __ StoreIntoObject(R0,
                     FieldAddress(R1, Array::data_offset()),
                     R2);
  // Caller is responsible for preserving the value if necessary.
  __ Ret();
  __ Bind(&fall_through);
}


// Allocate a GrowableObjectArray using the backing array specified.
// On stack: type argument (+1), data (+0).
void Intrinsifier::GrowableList_Allocate(Assembler* assembler) {
  // The newly allocated object is returned in R0.
  const intptr_t kTypeArgumentsOffset = 1 * kWordSize;
  const intptr_t kArrayOffset = 0 * kWordSize;
  Label fall_through;

  // Compute the size to be allocated, it is based on the array length
  // and is computed as:
  // RoundedAllocationSize(sizeof(RawGrowableObjectArray)) +
  intptr_t fixed_size = GrowableObjectArray::InstanceSize();

  Isolate* isolate = Isolate::Current();
  Heap* heap = isolate->heap();

  __ LoadImmediate(R2, heap->TopAddress());
  __ ldr(R0, Address(R2, 0));
  __ AddImmediate(R1, R0, fixed_size);

  // Check if the allocation fits into the remaining space.
  // R0: potential new backing array object start.
  // R1: potential next object start.
  __ LoadImmediate(R3, heap->EndAddress());
  __ ldr(R3, Address(R3, 0));
  __ cmp(R1, ShifterOperand(R3));
  __ b(&fall_through, CS);

  // Successfully allocated the object(s), now update top to point to
  // next object start and initialize the object.
  __ str(R1, Address(R2, 0));
  __ AddImmediate(R0, kHeapObjectTag);

  // Initialize the tags.
  // R0: new growable array object start as a tagged pointer.
  const Class& cls = Class::Handle(
      isolate->object_store()->growable_object_array_class());
  uword tags = 0;
  tags = RawObject::SizeTag::update(fixed_size, tags);
  tags = RawObject::ClassIdTag::update(cls.id(), tags);
  __ LoadImmediate(R1, tags);
  __ str(R1, FieldAddress(R0, GrowableObjectArray::tags_offset()));

  // Store backing array object in growable array object.
  __ ldr(R1, Address(SP, kArrayOffset));  // Data argument.
  // R0 is new, no barrier needed.
  __ StoreIntoObjectNoBarrier(
      R0,
      FieldAddress(R0, GrowableObjectArray::data_offset()),
      R1);

  // R0: new growable array object start as a tagged pointer.
  // Store the type argument field in the growable array object.
  __ ldr(R1, Address(SP, kTypeArgumentsOffset));  // Type argument.
  __ StoreIntoObjectNoBarrier(
      R0,
      FieldAddress(R0, GrowableObjectArray::type_arguments_offset()),
      R1);

  // Set the length field in the growable array object to 0.
  __ LoadImmediate(R1, 0);
  __ str(R1, FieldAddress(R0, GrowableObjectArray::length_offset()));
  __ Ret();  // Returns the newly allocated object in R0.

  __ Bind(&fall_through);
}


void Intrinsifier::GrowableList_getLength(Assembler* assembler) {
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ ldr(R0, FieldAddress(R0, GrowableObjectArray::length_offset()));
  __ Ret();
}


void Intrinsifier::GrowableList_getCapacity(Assembler* assembler) {
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ ldr(R0, FieldAddress(R0, GrowableObjectArray::data_offset()));
  __ ldr(R0, FieldAddress(R0, Array::length_offset()));
  __ Ret();
}


void Intrinsifier::GrowableList_getIndexed(Assembler* assembler) {
  Label fall_through;

  __ ldr(R0, Address(SP, + 0 * kWordSize));  // Index
  __ ldr(R1, Address(SP, + 1 * kWordSize));  // Array

  __ tst(R0, ShifterOperand(kSmiTagMask));
  __ b(&fall_through, NE);  // Index is not an smi, fall through

  // range check
  __ ldr(R6, FieldAddress(R1, GrowableObjectArray::length_offset()));
  __ cmp(R0, ShifterOperand(R6));

  ASSERT(kSmiTagShift == 1);
  // array element at R6 + R0 * 2 + Array::data_offset - 1
  __ ldr(R6, FieldAddress(R1, GrowableObjectArray::data_offset()), CC);  // data
  __ add(R6, R6, ShifterOperand(R0, LSL, 1), CC);
  __ ldr(R0, FieldAddress(R6, Array::data_offset()), CC);
  __ bx(LR, CC);
  __ Bind(&fall_through);
}


// Set value into growable object array at specified index.
// On stack: growable array (+2), index (+1), value (+0).
void Intrinsifier::GrowableList_setIndexed(Assembler* assembler) {
  if (FLAG_enable_type_checks) {
    return;
  }
  Label fall_through;
  __ ldr(R1, Address(SP, 1 * kWordSize));  // Index.
  __ ldr(R0, Address(SP, 2 * kWordSize));  // GrowableArray.
  __ tst(R1, ShifterOperand(kSmiTagMask));
  __ b(&fall_through, NE);  // Non-smi index.
  // Range check using _length field.
  __ ldr(R2, FieldAddress(R0, GrowableObjectArray::length_offset()));
  __ cmp(R1, ShifterOperand(R2));
  // Runtime throws exception.
  __ b(&fall_through, CS);
  __ ldr(R0, FieldAddress(R0, GrowableObjectArray::data_offset()));  // data.
  __ ldr(R2, Address(SP, 0 * kWordSize));  // Value.
  // Note that R1 is Smi, i.e, times 2.
  ASSERT(kSmiTagShift == 1);
  __ add(R1, R0, ShifterOperand(R1, LSL, 1));
  __ StoreIntoObject(R0,
                     FieldAddress(R1, Array::data_offset()),
                     R2);
  __ Ret();
  __ Bind(&fall_through);
}


// Set length of growable object array. The length cannot
// be greater than the length of the data container.
// On stack: growable array (+1), length (+0).
void Intrinsifier::GrowableList_setLength(Assembler* assembler) {
  __ ldr(R0, Address(SP, 1 * kWordSize));  // Growable array.
  __ ldr(R1, Address(SP, 0 * kWordSize));  // Length value.
  __ tst(R1, ShifterOperand(kSmiTagMask));  // Check for Smi.
  __ str(R1, FieldAddress(R0, GrowableObjectArray::length_offset()), EQ);
  __ bx(LR, EQ);
  // Fall through on non-Smi.
}


// Set data of growable object array.
// On stack: growable array (+1), data (+0).
void Intrinsifier::GrowableList_setData(Assembler* assembler) {
  if (FLAG_enable_type_checks) {
    return;
  }
  Label fall_through;
  __ ldr(R1, Address(SP, 0 * kWordSize));  // Data.
  // Check that data is an ObjectArray.
  __ tst(R1, ShifterOperand(kSmiTagMask));
  __ b(&fall_through, EQ);  // Data is Smi.
  __ CompareClassId(R1, kArrayCid, R0);
  __ b(&fall_through, NE);
  __ ldr(R0, Address(SP, 1 * kWordSize));  // Growable array.
  __ StoreIntoObject(R0,
                     FieldAddress(R0, GrowableObjectArray::data_offset()),
                     R1);
  __ Ret();
  __ Bind(&fall_through);
}


// Add an element to growable array if it doesn't need to grow, otherwise
// call into regular code.
// On stack: growable array (+1), value (+0).
void Intrinsifier::GrowableList_add(Assembler* assembler) {
  // In checked mode we need to type-check the incoming argument.
  if (FLAG_enable_type_checks) return;
  Label fall_through;
  // R0: Array.
  __ ldr(R0, Address(SP, 1 * kWordSize));
  // R1: length.
  __ ldr(R1, FieldAddress(R0, GrowableObjectArray::length_offset()));
  // R2: data.
  __ ldr(R2, FieldAddress(R0, GrowableObjectArray::data_offset()));
  // R3: capacity.
  __ ldr(R3, FieldAddress(R2, Array::length_offset()));
  // Compare length with capacity.
  __ cmp(R1, ShifterOperand(R3));
  __ b(&fall_through, EQ);  // Must grow data.
  const int32_t value_one = reinterpret_cast<int32_t>(Smi::New(1));
  // len = len + 1;
  __ add(R3, R1, ShifterOperand(value_one));
  __ str(R3, FieldAddress(R0, GrowableObjectArray::length_offset()));
  __ ldr(R0, Address(SP, 0 * kWordSize));  // Value.
  ASSERT(kSmiTagShift == 1);
  __ add(R1, R2, ShifterOperand(R1, LSL, 1));
  __ StoreIntoObject(R2,
                     FieldAddress(R1, Array::data_offset()),
                     R0);
  const int32_t raw_null = reinterpret_cast<int32_t>(Object::null());
  __ LoadImmediate(R0, raw_null);
  __ Ret();
  __ Bind(&fall_through);
}


#define TYPED_ARRAY_ALLOCATION(type_name, cid, max_len, scale_shift)           \
  Label fall_through;                                                          \
  const intptr_t kArrayLengthStackOffset = 0 * kWordSize;                      \
  __ ldr(R2, Address(SP, kArrayLengthStackOffset));  /* Array length. */       \
  /* Check that length is a positive Smi. */                                   \
  /* R2: requested array length argument. */                                   \
  __ tst(R2, ShifterOperand(kSmiTagMask));                                     \
  __ b(&fall_through, NE);                                                     \
  __ CompareImmediate(R2, 0);                                                  \
  __ b(&fall_through, LT);                                                     \
  __ SmiUntag(R2);                                                             \
  /* Check for maximum allowed length. */                                      \
  /* R2: untagged array length. */                                             \
  __ CompareImmediate(R2, max_len);                                            \
  __ b(&fall_through, GT);                                                     \
  __ mov(R2, ShifterOperand(R2, LSL, scale_shift));                            \
  const intptr_t fixed_size = sizeof(Raw##type_name) + kObjectAlignment - 1;   \
  __ AddImmediate(R2, fixed_size);                                             \
  __ bic(R2, R2, ShifterOperand(kObjectAlignment - 1));                        \
  Heap* heap = Isolate::Current()->heap();                                     \
                                                                               \
  __ LoadImmediate(R0, heap->TopAddress());                                    \
  __ ldr(R0, Address(R0, 0));                                                  \
                                                                               \
  /* R2: allocation size. */                                                   \
  __ add(R1, R0, ShifterOperand(R2));                                          \
  __ b(&fall_through, VS);                                                     \
                                                                               \
  /* Check if the allocation fits into the remaining space. */                 \
  /* R0: potential new object start. */                                        \
  /* R1: potential next object start. */                                       \
  /* R2: allocation size. */                                                   \
  __ LoadImmediate(R3, heap->EndAddress());                                    \
  __ ldr(R3, Address(R3, 0));                                                  \
  __ cmp(R1, ShifterOperand(R3));                                              \
  __ b(&fall_through, CS);                                                     \
                                                                               \
  /* Successfully allocated the object(s), now update top to point to */       \
  /* next object start and initialize the object. */                           \
  __ LoadImmediate(R3, heap->TopAddress());                                    \
  __ str(R1, Address(R3, 0));                                                  \
  __ AddImmediate(R0, kHeapObjectTag);                                         \
                                                                               \
  /* Initialize the tags. */                                                   \
  /* R0: new object start as a tagged pointer. */                              \
  /* R1: new object end address. */                                            \
  /* R2: allocation size. */                                                   \
  {                                                                            \
    __ CompareImmediate(R2, RawObject::SizeTag::kMaxSizeTag);                  \
    __ mov(R2, ShifterOperand(R2, LSL,                                         \
        RawObject::kSizeTagBit - kObjectAlignmentLog2), LS);                   \
    __ mov(R2, ShifterOperand(0), HI);                                         \
                                                                               \
    /* Get the class index and insert it into the tags. */                     \
    __ LoadImmediate(TMP, RawObject::ClassIdTag::encode(cid));                 \
    __ orr(R2, R2, ShifterOperand(TMP));                                       \
    __ str(R2, FieldAddress(R0, type_name::tags_offset()));  /* Tags. */       \
  }                                                                            \
  /* Set the length field. */                                                  \
  /* R0: new object start as a tagged pointer. */                              \
  /* R1: new object end address. */                                            \
  __ ldr(R2, Address(SP, kArrayLengthStackOffset));  /* Array length. */       \
  __ StoreIntoObjectNoBarrier(R0,                                              \
                              FieldAddress(R0, type_name::length_offset()),    \
                              R2);                                             \
  /* Initialize all array elements to 0. */                                    \
  /* R0: new object start as a tagged pointer. */                              \
  /* R1: new object end address. */                                            \
  /* R2: iterator which initially points to the start of the variable */       \
  /* R3: scratch register. */                                                  \
  /* data area to be initialized. */                                           \
  __ LoadImmediate(R3, 0);                                                     \
  __ AddImmediate(R2, R0, sizeof(Raw##type_name) - 1);                         \
  Label init_loop;                                                             \
  __ Bind(&init_loop);                                                         \
  __ cmp(R2, ShifterOperand(R1));                                              \
  __ str(R3, Address(R2, 0), CC);                                              \
  __ add(R2, R2, ShifterOperand(kWordSize), CC);                               \
  __ b(&init_loop, CC);                                                        \
                                                                               \
  __ Ret();                                                                    \
  __ Bind(&fall_through);                                                      \


// Gets the length of a TypedData.
void Intrinsifier::TypedData_getLength(Assembler* assembler) {
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ ldr(R0, FieldAddress(R0, TypedData::length_offset()));
  __ Ret();
}


static int GetScaleFactor(intptr_t size) {
  switch (size) {
    case 1: return 0;
    case 2: return 1;
    case 4: return 2;
    case 8: return 3;
    case 16: return 4;
  }
  UNREACHABLE();
  return -1;
};


#define TYPED_DATA_ALLOCATOR(clazz)                                            \
void Intrinsifier::TypedData_##clazz##_new(Assembler* assembler) {             \
  intptr_t size = TypedData::ElementSizeInBytes(kTypedData##clazz##Cid);       \
  intptr_t max_len = TypedData::MaxElements(kTypedData##clazz##Cid);           \
  int shift = GetScaleFactor(size);                                            \
  TYPED_ARRAY_ALLOCATION(TypedData, kTypedData##clazz##Cid, max_len, shift);   \
}                                                                              \
void Intrinsifier::TypedData_##clazz##_factory(Assembler* assembler) {         \
  intptr_t size = TypedData::ElementSizeInBytes(kTypedData##clazz##Cid);       \
  intptr_t max_len = TypedData::MaxElements(kTypedData##clazz##Cid);           \
  int shift = GetScaleFactor(size);                                            \
  TYPED_ARRAY_ALLOCATION(TypedData, kTypedData##clazz##Cid, max_len, shift);   \
}
CLASS_LIST_TYPED_DATA(TYPED_DATA_ALLOCATOR)
#undef TYPED_DATA_ALLOCATOR


// Loads args from stack into R0 and R1
// Tests if they are smis, jumps to label not_smi if not.
static void TestBothArgumentsSmis(Assembler* assembler, Label* not_smi) {
  __ ldr(R0, Address(SP, + 0 * kWordSize));
  __ ldr(R1, Address(SP, + 1 * kWordSize));
  __ orr(TMP, R0, ShifterOperand(R1));
  __ tst(TMP, ShifterOperand(kSmiTagMask));
  __ b(not_smi, NE);
  return;
}


void Intrinsifier::Integer_addFromInteger(Assembler* assembler) {
  Label fall_through;
  TestBothArgumentsSmis(assembler, &fall_through);  // Checks two smis.
  __ adds(R0, R0, ShifterOperand(R1));  // Adds.
  __ bx(LR, VC);  // Return if no overflow.
  // Otherwise fall through.
  __ Bind(&fall_through);
}


void Intrinsifier::Integer_add(Assembler* assembler) {
  return Integer_addFromInteger(assembler);
}


void Intrinsifier::Integer_subFromInteger(Assembler* assembler) {
  Label fall_through;
  TestBothArgumentsSmis(assembler, &fall_through);
  __ subs(R0, R0, ShifterOperand(R1));  // Subtract.
  __ bx(LR, VC);  // Return if no overflow.
  // Otherwise fall through.
  __ Bind(&fall_through);
}


void Intrinsifier::Integer_sub(Assembler* assembler) {
  Label fall_through;
  TestBothArgumentsSmis(assembler, &fall_through);
  __ subs(R0, R1, ShifterOperand(R0));  // Subtract.
  __ bx(LR, VC);  // Return if no overflow.
  // Otherwise fall through.
  __ Bind(&fall_through);
}


void Intrinsifier::Integer_mulFromInteger(Assembler* assembler) {
  Label fall_through;

  TestBothArgumentsSmis(assembler, &fall_through);  // checks two smis
  __ SmiUntag(R0);  // untags R6. only want result shifted by one

  __ smull(R0, IP, R0, R1);  // IP:R0 <- R0 * R1.
  __ cmp(IP, ShifterOperand(R0, ASR, 31));
  __ bx(LR, EQ);
  __ Bind(&fall_through);  // Fall through on overflow.
}


void Intrinsifier::Integer_mul(Assembler* assembler) {
  return Integer_mulFromInteger(assembler);
}


// Optimizations:
// - result is 0 if:
//   - left is 0
//   - left equals right
// - result is left if
//   - left > 0 && left < right
// R1: Tagged left (dividend).
// R0: Tagged right (divisor).
// Returns with result in R0, OR:
//   R1: Untagged result (remainder).
static void EmitRemainderOperation(Assembler* assembler) {
  Label modulo;
  const Register left = R1;
  const Register right = R0;
  const Register result = R1;
  const Register tmp = R2;
  ASSERT(left == result);

  // Check for quick zero results.
  __ cmp(left, ShifterOperand(0));
  __ mov(R0, ShifterOperand(0), EQ);
  __ bx(LR, EQ);  // left is 0? Return 0.
  __ cmp(left, ShifterOperand(right));
  __ mov(R0, ShifterOperand(0), EQ);
  __ bx(LR, EQ);  // left == right? Return 0.

  // Check if result should be left.
  __ cmp(left, ShifterOperand(0));
  __ b(&modulo, LT);
  // left is positive.
  __ cmp(left, ShifterOperand(right));
  // left is less than right, result is left.
  __ mov(R0, ShifterOperand(left), LT);
  __ bx(LR, LT);

  __ Bind(&modulo);
  // result <- left - right * (left / right)
  __ SmiUntag(left);
  __ SmiUntag(right);

  __ IntegerDivide(tmp, left, right, D1, D0);

  __ mls(result, right, tmp, left);  // result <- left - right * TMP
  return;
}


// Implementation:
//  res = left % right;
//  if (res < 0) {
//    if (right < 0) {
//      res = res - right;
//    } else {
//      res = res + right;
//    }
//  }
void Intrinsifier::Integer_moduloFromInteger(Assembler* assembler) {
  // Check to see if we have integer division
  Label fall_through, subtract;
  __ ldr(R1, Address(SP, + 0 * kWordSize));
  __ ldr(R0, Address(SP, + 1 * kWordSize));
  __ orr(TMP, R0, ShifterOperand(R1));
  __ tst(TMP, ShifterOperand(kSmiTagMask));
  __ b(&fall_through, NE);
  // R1: Tagged left (dividend).
  // R0: Tagged right (divisor).
  // Check if modulo by zero -> exception thrown in main function.
  __ cmp(R0, ShifterOperand(0));
  __ b(&fall_through, EQ);
  EmitRemainderOperation(assembler);
  // Untagged right in R0. Untagged remainder result in R1.

  __ cmp(R1, ShifterOperand(0));
  __ mov(R0, ShifterOperand(R1, LSL, 1), GE);  // Tag and move result to R0.
  __ bx(LR, GE);

  // Result is negative, adjust it.
  __ cmp(R0, ShifterOperand(0));
  __ sub(R0, R1, ShifterOperand(R0), LT);
  __ add(R0, R1, ShifterOperand(R0), GE);
  __ SmiTag(R0);
  __ Ret();

  __ Bind(&fall_through);
}


void Intrinsifier::Integer_remainder(Assembler* assembler) {
  // Check to see if we have integer division
  Label fall_through;
  TestBothArgumentsSmis(assembler, &fall_through);
  // R1: Tagged left (dividend).
  // R0: Tagged right (divisor).
  // Check if modulo by zero -> exception thrown in main function.
  __ cmp(R0, ShifterOperand(0));
  __ b(&fall_through, EQ);
  EmitRemainderOperation(assembler);
  // Untagged remainder result in R1.
  __ mov(R0, ShifterOperand(R1, LSL, 1));  // Tag result and return.
  __ Ret();

  __ Bind(&fall_through);
}


void Intrinsifier::Integer_truncDivide(Assembler* assembler) {
  // Check to see if we have integer division
  Label fall_through;

  TestBothArgumentsSmis(assembler, &fall_through);
  __ cmp(R0, ShifterOperand(0));
  __ b(&fall_through, EQ);  // If b is 0, fall through.

  __ SmiUntag(R0);
  __ SmiUntag(R1);

  __ IntegerDivide(R0, R1, R0, D1, D0);

  // Check the corner case of dividing the 'MIN_SMI' with -1, in which case we
  // cannot tag the result.
  __ CompareImmediate(R0, 0x40000000);
  __ SmiTag(R0, NE);  // Not equal. Okay to tag and return.
  __ bx(LR, NE);  // Return.
  __ Bind(&fall_through);
}


void Intrinsifier::Integer_negate(Assembler* assembler) {
  Label fall_through;
  __ ldr(R0, Address(SP, + 0 * kWordSize));  // Grab first argument.
  __ tst(R0, ShifterOperand(kSmiTagMask));  // Test for Smi.
  __ b(&fall_through, NE);
  __ rsbs(R0, R0, ShifterOperand(0));  // R0 is a Smi. R0 <- 0 - R0.
  __ bx(LR, VC);  // Return if there wasn't overflow, fall through otherwise.
  // R0 is not a Smi. Fall through.
  __ Bind(&fall_through);
}


void Intrinsifier::Integer_bitAndFromInteger(Assembler* assembler) {
  Label fall_through;

  TestBothArgumentsSmis(assembler, &fall_through);  // checks two smis
  __ and_(R0, R0, ShifterOperand(R1));

  __ Ret();
  __ Bind(&fall_through);
}


void Intrinsifier::Integer_bitAnd(Assembler* assembler) {
  return Integer_bitAndFromInteger(assembler);
}


void Intrinsifier::Integer_bitOrFromInteger(Assembler* assembler) {
  Label fall_through;

  TestBothArgumentsSmis(assembler, &fall_through);  // checks two smis
  __ orr(R0, R0, ShifterOperand(R1));

  __ Ret();
  __ Bind(&fall_through);
}


void Intrinsifier::Integer_bitOr(Assembler* assembler) {
  return Integer_bitOrFromInteger(assembler);
}


void Intrinsifier::Integer_bitXorFromInteger(Assembler* assembler) {
  Label fall_through;

  TestBothArgumentsSmis(assembler, &fall_through);  // checks two smis
  __ eor(R0, R0, ShifterOperand(R1));

  __ Ret();
  __ Bind(&fall_through);
}


void Intrinsifier::Integer_bitXor(Assembler* assembler) {
  return Integer_bitXorFromInteger(assembler);
}


void Intrinsifier::Integer_shl(Assembler* assembler) {
  ASSERT(kSmiTagShift == 1);
  ASSERT(kSmiTag == 0);
  Label fall_through;

  TestBothArgumentsSmis(assembler, &fall_through);
  __ CompareImmediate(R0, Smi::RawValue(Smi::kBits));
  __ b(&fall_through, HI);

  __ SmiUntag(R0);

  // Check for overflow by shifting left and shifting back arithmetically.
  // If the result is different from the original, there was overflow.
  __ mov(IP, ShifterOperand(R1, LSL, R0));
  __ cmp(R1, ShifterOperand(IP, ASR, R0));

  // No overflow, result in R0.
  __ mov(R0, ShifterOperand(R1, LSL, R0), EQ);
  __ bx(LR, EQ);

  // Arguments are Smi but the shift produced an overflow to Mint.
  __ CompareImmediate(R1, 0);
  __ b(&fall_through, LT);
  __ SmiUntag(R1);

  // Pull off high bits that will be shifted off of R1 by making a mask
  // ((1 << R0) - 1), shifting it to the left, masking R1, then shifting back.
  // high bits = (((1 << R0) - 1) << (32 - R0)) & R1) >> (32 - R0)
  // lo bits = R1 << R0
  __ LoadImmediate(R7, 1);
  __ mov(R7, ShifterOperand(R7, LSL, R0));  // R7 <- 1 << R0
  __ sub(R7, R7, ShifterOperand(1));  // R7 <- R7 - 1
  __ rsb(R8, R0, ShifterOperand(32));  // R8 <- 32 - R0
  __ mov(R7, ShifterOperand(R7, LSL, R8));  // R7 <- R7 << R8
  __ and_(R7, R1, ShifterOperand(R7));  // R7 <- R7 & R1
  __ mov(R7, ShifterOperand(R7, LSR, R8));  // R7 <- R7 >> R8
  // Now R7 has the bits that fall off of R1 on a left shift.
  __ mov(R1, ShifterOperand(R1, LSL, R0));  // R1 gets the low bits.

  const Class& mint_class = Class::Handle(
      Isolate::Current()->object_store()->mint_class());
  __ TryAllocate(mint_class, &fall_through, R0);


  __ str(R1, FieldAddress(R0, Mint::value_offset()));
  __ str(R7, FieldAddress(R0, Mint::value_offset() + kWordSize));
  __ Ret();
  __ Bind(&fall_through);
}


static void Get64SmiOrMint(Assembler* assembler,
                           Register res_hi,
                           Register res_lo,
                           Register reg,
                           Label* not_smi_or_mint) {
  Label not_smi, done;
  __ tst(reg, ShifterOperand(kSmiTagMask));
  __ b(&not_smi, NE);
  __ SmiUntag(reg);

  // Sign extend to 64 bit
  __ mov(res_lo, ShifterOperand(reg));
  __ mov(res_hi, ShifterOperand(res_lo, ASR, 31));
  __ b(&done);

  __ Bind(&not_smi);
  __ CompareClassId(reg, kMintCid, res_lo);
  __ b(not_smi_or_mint, NE);

  // Mint.
  __ ldr(res_lo, FieldAddress(reg, Mint::value_offset()));
  __ ldr(res_hi, FieldAddress(reg, Mint::value_offset() + kWordSize));
  __ Bind(&done);
  return;
}


static void CompareIntegers(Assembler* assembler, Condition true_condition) {
  Label try_mint_smi, is_true, is_false, drop_two_fall_through, fall_through;
  TestBothArgumentsSmis(assembler, &try_mint_smi);
  // R0 contains the right argument. R1 contains left argument

  __ cmp(R1, ShifterOperand(R0));
  __ b(&is_true, true_condition);
  __ Bind(&is_false);
  __ LoadObject(R0, Bool::False());
  __ Ret();
  __ Bind(&is_true);
  __ LoadObject(R0, Bool::True());
  __ Ret();

  // 64-bit comparison
  Condition hi_true_cond, hi_false_cond, lo_false_cond;
  switch (true_condition) {
    case LT:
    case LE:
      hi_true_cond = LT;
      hi_false_cond = GT;
      lo_false_cond = (true_condition == LT) ? CS : HI;
      break;
    case GT:
    case GE:
      hi_true_cond = GT;
      hi_false_cond = LT;
      lo_false_cond = (true_condition == GT) ? LS : CC;
      break;
    default:
      UNREACHABLE();
      hi_true_cond = hi_false_cond = lo_false_cond = VS;
  }

  __ Bind(&try_mint_smi);
  // Get left as 64 bit integer.
  Get64SmiOrMint(assembler, R3, R2, R1, &fall_through);
  // Get right as 64 bit integer.
  Get64SmiOrMint(assembler, R7, R6, R0, &fall_through);
  // R3: left high.
  // R2: left low.
  // R7: right high.
  // R6: right low.

  __ cmp(R3, ShifterOperand(R7));  // Compare left hi, right high.
  __ b(&is_false, hi_false_cond);
  __ b(&is_true, hi_true_cond);
  __ cmp(R2, ShifterOperand(R6));  // Compare left lo, right lo.
  __ b(&is_false, lo_false_cond);
  // Else is true.
  __ b(&is_true);

  __ Bind(&fall_through);
}


void Intrinsifier::Integer_greaterThanFromInt(Assembler* assembler) {
  return CompareIntegers(assembler, LT);
}


void Intrinsifier::Integer_lessThan(Assembler* assembler) {
  return Integer_greaterThanFromInt(assembler);
}


void Intrinsifier::Integer_greaterThan(Assembler* assembler) {
  return CompareIntegers(assembler, GT);
}


void Intrinsifier::Integer_lessEqualThan(Assembler* assembler) {
  return CompareIntegers(assembler, LE);
}


void Intrinsifier::Integer_greaterEqualThan(Assembler* assembler) {
  return CompareIntegers(assembler, GE);
}


// This is called for Smi, Mint and Bigint receivers. The right argument
// can be Smi, Mint, Bigint or double.
void Intrinsifier::Integer_equalToInteger(Assembler* assembler) {
  Label fall_through, true_label, check_for_mint;
  // For integer receiver '===' check first.
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ ldr(R1, Address(SP, 1 * kWordSize));
  __ cmp(R0, ShifterOperand(R1));
  __ b(&true_label, EQ);

  __ orr(R2, R0, ShifterOperand(R1));
  __ tst(R2, ShifterOperand(kSmiTagMask));
  __ b(&check_for_mint, NE);  // If R0 or R1 is not a smi do Mint checks.

  // Both arguments are smi, '===' is good enough.
  __ LoadObject(R0, Bool::False());
  __ Ret();
  __ Bind(&true_label);
  __ LoadObject(R0, Bool::True());
  __ Ret();

  // At least one of the arguments was not Smi.
  Label receiver_not_smi;
  __ Bind(&check_for_mint);

  __ tst(R1, ShifterOperand(kSmiTagMask));  // Check receiver.
  __ b(&receiver_not_smi, NE);

  // Left (receiver) is Smi, return false if right is not Double.
  // Note that an instance of Mint or Bigint never contains a value that can be
  // represented by Smi.

  __ CompareClassId(R0, kDoubleCid, R2);
  __ b(&fall_through, EQ);
  __ LoadObject(R0, Bool::False());  // Smi == Mint -> false.
  __ Ret();

  __ Bind(&receiver_not_smi);
  // R1:: receiver.

  __ CompareClassId(R1, kMintCid, R2);
  __ b(&fall_through, NE);
  // Receiver is Mint, return false if right is Smi.
  __ tst(R0, ShifterOperand(kSmiTagMask));
  __ b(&fall_through, NE);
  __ LoadObject(R0, Bool::False());
  __ Ret();
  // TODO(srdjan): Implement Mint == Mint comparison.

  __ Bind(&fall_through);
}


void Intrinsifier::Integer_equal(Assembler* assembler) {
  return Integer_equalToInteger(assembler);
}


void Intrinsifier::Integer_sar(Assembler* assembler) {
  Label fall_through;

  TestBothArgumentsSmis(assembler, &fall_through);
  // Shift amount in R0. Value to shift in R1.

  // Fall through if shift amount is negative.
  __ SmiUntag(R0);
  __ CompareImmediate(R0, 0);
  __ b(&fall_through, LT);

  // If shift amount is bigger than 31, set to 31.
  __ CompareImmediate(R0, 0x1F);
  __ LoadImmediate(R0, 0x1F, GT);
  __ SmiUntag(R1);
  __ mov(R0, ShifterOperand(R1, ASR, R0));
  __ SmiTag(R0);
  __ Ret();
  __ Bind(&fall_through);
}


void Intrinsifier::Smi_bitNegate(Assembler* assembler) {
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ mvn(R0, ShifterOperand(R0));
  __ bic(R0, R0, ShifterOperand(kSmiTagMask));  // Remove inverted smi-tag.
  __ Ret();
}


void Intrinsifier::Smi_bitLength(Assembler* assembler) {
  // TODO(sra): Implement as word-length - CLZ.
}


// Check if the last argument is a double, jump to label 'is_smi' if smi
// (easy to convert to double), otherwise jump to label 'not_double_smi',
// Returns the last argument in R0.
static void TestLastArgumentIsDouble(Assembler* assembler,
                                     Label* is_smi,
                                     Label* not_double_smi) {
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ tst(R0, ShifterOperand(kSmiTagMask));
  __ b(is_smi, EQ);
  __ CompareClassId(R0, kDoubleCid, R1);
  __ b(not_double_smi, NE);
  // Fall through with Double in R0.
}


// Both arguments on stack, arg0 (left) is a double, arg1 (right) is of unknown
// type. Return true or false object in the register R0. Any NaN argument
// returns false. Any non-double arg1 causes control flow to fall through to the
// slow case (compiled method body).
static void CompareDoubles(Assembler* assembler, Condition true_condition) {
  Label fall_through, is_smi, double_op;

  TestLastArgumentIsDouble(assembler, &is_smi, &fall_through);
  // Both arguments are double, right operand is in R0.

  __ LoadDFromOffset(D1, R0, Double::value_offset() - kHeapObjectTag);
  __ Bind(&double_op);
  __ ldr(R0, Address(SP, 1 * kWordSize));  // Left argument.
  __ LoadDFromOffset(D0, R0, Double::value_offset() - kHeapObjectTag);

  __ vcmpd(D0, D1);
  __ vmstat();
  __ LoadObject(R0, Bool::False());
  // Return false if D0 or D1 was NaN before checking true condition.
  __ bx(LR, VS);
  __ LoadObject(R0, Bool::True(), true_condition);
  __ Ret();

  __ Bind(&is_smi);  // Convert R0 to a double.
  __ SmiUntag(R0);
  __ vmovsr(S0, R0);
  __ vcvtdi(D1, S0);
  __ b(&double_op);  // Then do the comparison.
  __ Bind(&fall_through);
}


void Intrinsifier::Double_greaterThan(Assembler* assembler) {
  return CompareDoubles(assembler, HI);
}


void Intrinsifier::Double_greaterEqualThan(Assembler* assembler) {
  return CompareDoubles(assembler, CS);
}


void Intrinsifier::Double_lessThan(Assembler* assembler) {
  return CompareDoubles(assembler, CC);
}


void Intrinsifier::Double_equal(Assembler* assembler) {
  return CompareDoubles(assembler, EQ);
}


void Intrinsifier::Double_lessEqualThan(Assembler* assembler) {
  return CompareDoubles(assembler, LS);
}


// Expects left argument to be double (receiver). Right argument is unknown.
// Both arguments are on stack.
static void DoubleArithmeticOperations(Assembler* assembler, Token::Kind kind) {
  Label fall_through;

  TestLastArgumentIsDouble(assembler, &fall_through, &fall_through);
  // Both arguments are double, right operand is in R0.
  __ LoadDFromOffset(D1, R0, Double::value_offset() - kHeapObjectTag);
  __ ldr(R0, Address(SP, 1 * kWordSize));  // Left argument.
  __ LoadDFromOffset(D0, R0, Double::value_offset() - kHeapObjectTag);
  switch (kind) {
    case Token::kADD: __ vaddd(D0, D0, D1); break;
    case Token::kSUB: __ vsubd(D0, D0, D1); break;
    case Token::kMUL: __ vmuld(D0, D0, D1); break;
    case Token::kDIV: __ vdivd(D0, D0, D1); break;
    default: UNREACHABLE();
  }
  const Class& double_class = Class::Handle(
      Isolate::Current()->object_store()->double_class());
  __ TryAllocate(double_class, &fall_through, R0);  // Result register.
  __ StoreDToOffset(D0, R0, Double::value_offset() - kHeapObjectTag);
  __ Ret();
  __ Bind(&fall_through);
}


void Intrinsifier::Double_add(Assembler* assembler) {
  return DoubleArithmeticOperations(assembler, Token::kADD);
}


void Intrinsifier::Double_mul(Assembler* assembler) {
  return DoubleArithmeticOperations(assembler, Token::kMUL);
}


void Intrinsifier::Double_sub(Assembler* assembler) {
  return DoubleArithmeticOperations(assembler, Token::kSUB);
}


void Intrinsifier::Double_div(Assembler* assembler) {
  return DoubleArithmeticOperations(assembler, Token::kDIV);
}


// Left is double right is integer (Bigint, Mint or Smi)
void Intrinsifier::Double_mulFromInteger(Assembler* assembler) {
  Label fall_through;
  // Only smis allowed.
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ tst(R0, ShifterOperand(kSmiTagMask));
  __ b(&fall_through, NE);
  // Is Smi.
  __ SmiUntag(R0);
  __ vmovsr(S0, R0);
  __ vcvtdi(D1, S0);
  __ ldr(R0, Address(SP, 1 * kWordSize));
  __ LoadDFromOffset(D0, R0, Double::value_offset() - kHeapObjectTag);
  __ vmuld(D0, D0, D1);
  const Class& double_class = Class::Handle(
      Isolate::Current()->object_store()->double_class());
  __ TryAllocate(double_class, &fall_through, R0);  // Result register.
  __ StoreDToOffset(D0, R0, Double::value_offset() - kHeapObjectTag);
  __ Ret();
  __ Bind(&fall_through);
}


void Intrinsifier::Double_fromInteger(Assembler* assembler) {
  Label fall_through;

  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ tst(R0, ShifterOperand(kSmiTagMask));
  __ b(&fall_through, NE);
  // Is Smi.
  __ SmiUntag(R0);
  __ vmovsr(S0, R0);
  __ vcvtdi(D0, S0);
  const Class& double_class = Class::Handle(
      Isolate::Current()->object_store()->double_class());
  __ TryAllocate(double_class, &fall_through, R0);  // Result register.
  __ StoreDToOffset(D0, R0, Double::value_offset() - kHeapObjectTag);
  __ Ret();
  __ Bind(&fall_through);
}


void Intrinsifier::Double_getIsNaN(Assembler* assembler) {
  Label is_true;
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ LoadDFromOffset(D0, R0, Double::value_offset() - kHeapObjectTag);
  __ vcmpd(D0, D0);
  __ vmstat();
  __ LoadObject(R0, Bool::False(), VC);
  __ LoadObject(R0, Bool::True(), VS);
  __ Ret();
}


void Intrinsifier::Double_getIsNegative(Assembler* assembler) {
  Label is_false, is_true, is_zero;
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ LoadDFromOffset(D0, R0, Double::value_offset() - kHeapObjectTag);
  __ LoadDImmediate(D1, 0.0, R1);
  __ vcmpd(D0, D1);
  __ vmstat();
  __ b(&is_false, VS);  // NaN -> false.
  __ b(&is_zero, EQ);  // Check for negative zero.
  __ b(&is_false, CS);  // >= 0 -> false.

  __ Bind(&is_true);
  __ LoadObject(R0, Bool::True());
  __ Ret();

  __ Bind(&is_false);
  __ LoadObject(R0, Bool::False());
  __ Ret();

  __ Bind(&is_zero);
  // Check for negative zero by looking at the sign bit.
  __ vmovrrd(R0, R1, D0);  // R1:R0 <- D0, so sign bit is in bit 31 of R1.
  __ mov(R1, ShifterOperand(R1, LSR, 31));
  __ tst(R1, ShifterOperand(1));
  __ b(&is_true, NE);  // Sign bit set.
  __ b(&is_false);
}


void Intrinsifier::Double_toInt(Assembler* assembler) {
  Label fall_through;

  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ LoadDFromOffset(D0, R0, Double::value_offset() - kHeapObjectTag);

  // Explicit NaN check, since ARM gives an FPU exception if you try to
  // convert NaN to an int.
  __ vcmpd(D0, D0);
  __ vmstat();
  __ b(&fall_through, VS);

  __ vcvtid(S0, D0);
  __ vmovrs(R0, S0);
  // Overflow is signaled with minint.
  // Check for overflow and that it fits into Smi.
  __ CompareImmediate(R0, 0xC0000000);
  __ b(&fall_through, MI);
  __ SmiTag(R0);
  __ Ret();
  __ Bind(&fall_through);
}


void Intrinsifier::Math_sqrt(Assembler* assembler) {
  Label fall_through, is_smi, double_op;
  TestLastArgumentIsDouble(assembler, &is_smi, &fall_through);
  // Argument is double and is in R0.
  __ LoadDFromOffset(D1, R0, Double::value_offset() - kHeapObjectTag);
  __ Bind(&double_op);
  __ vsqrtd(D0, D1);
  const Class& double_class = Class::Handle(
      Isolate::Current()->object_store()->double_class());
  __ TryAllocate(double_class, &fall_through, R0);  // Result register.
  __ StoreDToOffset(D0, R0, Double::value_offset() - kHeapObjectTag);
  __ Ret();
  __ Bind(&is_smi);
  __ SmiUntag(R0);
  __ vmovsr(S0, R0);
  __ vcvtdi(D1, S0);
  __ b(&double_op);
  __ Bind(&fall_through);
}


void Intrinsifier::Math_sin(Assembler* assembler) {
}


void Intrinsifier::Math_cos(Assembler* assembler) {
}


//    var state = ((_A * (_state[kSTATE_LO])) + _state[kSTATE_HI]) & _MASK_64;
//    _state[kSTATE_LO] = state & _MASK_32;
//    _state[kSTATE_HI] = state >> 32;
void Intrinsifier::Random_nextState(Assembler* assembler) {
  const Library& math_lib = Library::Handle(Library::MathLibrary());
  ASSERT(!math_lib.IsNull());
  const Class& random_class = Class::Handle(
      math_lib.LookupClassAllowPrivate(Symbols::_Random()));
  ASSERT(!random_class.IsNull());
  const Field& state_field = Field::ZoneHandle(
      random_class.LookupInstanceField(Symbols::_state()));
  ASSERT(!state_field.IsNull());
  const Field& random_A_field = Field::ZoneHandle(
      random_class.LookupStaticField(Symbols::_A()));
  ASSERT(!random_A_field.IsNull());
  ASSERT(random_A_field.is_const());
  const Instance& a_value = Instance::Handle(random_A_field.value());
  const int64_t a_int_value = Integer::Cast(a_value).AsInt64Value();
  // 'a_int_value' is a mask.
  ASSERT(Utils::IsUint(32, a_int_value));
  int32_t a_int32_value = static_cast<int32_t>(a_int_value);

  __ ldr(R0, Address(SP, 0 * kWordSize));  // Receiver.
  __ ldr(R1, FieldAddress(R0, state_field.Offset()));  // Field '_state'.
  // Addresses of _state[0] and _state[1].

  const int64_t disp_0 =
      FlowGraphCompiler::DataOffsetFor(kTypedDataUint32ArrayCid);

  const int64_t disp_1 =
      FlowGraphCompiler::ElementSizeFor(kTypedDataUint32ArrayCid) +
      FlowGraphCompiler::DataOffsetFor(kTypedDataUint32ArrayCid);

  __ LoadImmediate(R0, a_int32_value);
  __ LoadFromOffset(kWord, R2, R1, disp_0 - kHeapObjectTag);
  __ LoadFromOffset(kWord, R3, R1, disp_1 - kHeapObjectTag);
  __ mov(R6, ShifterOperand(0));  // Zero extend unsigned _state[kSTATE_HI].
  // Unsigned 32-bit multiply and 64-bit accumulate into R6:R3.
  __ umlal(R3, R6, R0, R2);  // R6:R3 <- R6:R3 + R0 * R2.
  __ StoreToOffset(kWord, R3, R1, disp_0 - kHeapObjectTag);
  __ StoreToOffset(kWord, R6, R1, disp_1 - kHeapObjectTag);
  __ Ret();
}


void Intrinsifier::Object_equal(Assembler* assembler) {
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ ldr(R1, Address(SP, 1 * kWordSize));
  __ cmp(R0, ShifterOperand(R1));
  __ LoadObject(R0, Bool::False(), NE);
  __ LoadObject(R0, Bool::True(), EQ);
  __ Ret();
}


void Intrinsifier::String_getHashCode(Assembler* assembler) {
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ ldr(R0, FieldAddress(R0, String::hash_offset()));
  __ cmp(R0, ShifterOperand(0));
  __ bx(LR, NE);  // Hash not yet computed.
}


void Intrinsifier::String_getLength(Assembler* assembler) {
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ ldr(R0, FieldAddress(R0, String::length_offset()));
  __ Ret();
}


void Intrinsifier::String_codeUnitAt(Assembler* assembler) {
  Label fall_through, try_two_byte_string;

  __ ldr(R1, Address(SP, 0 * kWordSize));  // Index.
  __ ldr(R0, Address(SP, 1 * kWordSize));  // String.
  __ tst(R1, ShifterOperand(kSmiTagMask));
  __ b(&fall_through, NE);  // Index is not a Smi.
  // Range check.
  __ ldr(R2, FieldAddress(R0, String::length_offset()));
  __ cmp(R1, ShifterOperand(R2));
  __ b(&fall_through, CS);  // Runtime throws exception.
  __ CompareClassId(R0, kOneByteStringCid, R3);
  __ b(&try_two_byte_string, NE);
  __ SmiUntag(R1);
  __ AddImmediate(R0, OneByteString::data_offset() - kHeapObjectTag);
  __ ldrb(R0, Address(R0, R1));
  __ SmiTag(R0);
  __ Ret();

  __ Bind(&try_two_byte_string);
  __ CompareClassId(R0, kTwoByteStringCid, R3);
  __ b(&fall_through, NE);
  ASSERT(kSmiTagShift == 1);
  __ AddImmediate(R0, OneByteString::data_offset() - kHeapObjectTag);
  __ ldrh(R0, Address(R0, R1));
  __ SmiTag(R0);
  __ Ret();

  __ Bind(&fall_through);
}


void Intrinsifier::String_getIsEmpty(Assembler* assembler) {
  __ ldr(R0, Address(SP, 0 * kWordSize));
  __ ldr(R0, FieldAddress(R0, String::length_offset()));
  __ cmp(R0, ShifterOperand(Smi::RawValue(0)));
  __ LoadObject(R0, Bool::True(), EQ);
  __ LoadObject(R0, Bool::False(), NE);
  __ Ret();
}


void Intrinsifier::OneByteString_getHashCode(Assembler* assembler) {
  __ ldr(R1, Address(SP, 0 * kWordSize));
  __ ldr(R0, FieldAddress(R1, String::hash_offset()));
  __ cmp(R0, ShifterOperand(0));
  __ bx(LR, NE);  // Return if already computed.

  __ ldr(R2, FieldAddress(R1, String::length_offset()));

  Label done;
  // If the string is empty, set the hash to 1, and return.
  __ cmp(R2, ShifterOperand(Smi::RawValue(0)));
  __ b(&done, EQ);

  __ SmiUntag(R2);
  __ mov(R3, ShifterOperand(0));
  __ AddImmediate(R6, R1, OneByteString::data_offset() - kHeapObjectTag);
  // R1: Instance of OneByteString.
  // R2: String length, untagged integer.
  // R3: Loop counter, untagged integer.
  // R6: String data.
  // R0: Hash code, untagged integer.

  Label loop;
  // Add to hash code: (hash_ is uint32)
  // hash_ += ch;
  // hash_ += hash_ << 10;
  // hash_ ^= hash_ >> 6;
  // Get one characters (ch).
  __ Bind(&loop);
  __ ldrb(R7, Address(R6, 0));
  // R7: ch.
  __ add(R3, R3, ShifterOperand(1));
  __ add(R6, R6, ShifterOperand(1));
  __ add(R0, R0, ShifterOperand(R7));
  __ add(R0, R0, ShifterOperand(R0, LSL, 10));
  __ eor(R0, R0, ShifterOperand(R0, LSR, 6));
  __ cmp(R3, ShifterOperand(R2));
  __ b(&loop, NE);

  // Finalize.
  // hash_ += hash_ << 3;
  // hash_ ^= hash_ >> 11;
  // hash_ += hash_ << 15;
  __ add(R0, R0, ShifterOperand(R0, LSL, 3));
  __ eor(R0, R0, ShifterOperand(R0, LSR, 11));
  __ add(R0, R0, ShifterOperand(R0, LSL, 15));
  // hash_ = hash_ & ((static_cast<intptr_t>(1) << bits) - 1);
  __ LoadImmediate(R2, (static_cast<intptr_t>(1) << String::kHashBits) - 1);
  __ and_(R0, R0, ShifterOperand(R2));
  __ cmp(R0, ShifterOperand(0));
  // return hash_ == 0 ? 1 : hash_;
  __ Bind(&done);
  __ mov(R0, ShifterOperand(1), EQ);
  __ SmiTag(R0);
  __ str(R0, FieldAddress(R1, String::hash_offset()));
  __ Ret();
}


// Allocates one-byte string of length 'end - start'. The content is not
// initialized.
// 'length-reg' (R2) contains tagged length.
// Returns new string as tagged pointer in R0.
static void TryAllocateOnebyteString(Assembler* assembler,
                                     Label* ok,
                                     Label* failure) {
  const Register length_reg = R2;
  Label fail;

  __ mov(R6, ShifterOperand(length_reg));  // Save the length register.
  __ SmiUntag(length_reg);
  const intptr_t fixed_size = sizeof(RawString) + kObjectAlignment - 1;
  __ AddImmediate(length_reg, fixed_size);
  __ bic(length_reg, length_reg, ShifterOperand(kObjectAlignment - 1));

  Isolate* isolate = Isolate::Current();
  Heap* heap = isolate->heap();

  __ LoadImmediate(R3, heap->TopAddress());
  __ ldr(R0, Address(R3, 0));

  // length_reg: allocation size.
  __ adds(R1, R0, ShifterOperand(length_reg));
  __ b(&fail, VS);  // Fail on overflow.

  // Check if the allocation fits into the remaining space.
  // R0: potential new object start.
  // R1: potential next object start.
  // R2: allocation size.
  // R3: heap->Top->Address().
  __ LoadImmediate(R7, heap->EndAddress());
  __ ldr(R7, Address(R7, 0));
  __ cmp(R1, ShifterOperand(R7));
  __ b(&fail, CS);

  // Successfully allocated the object(s), now update top to point to
  // next object start and initialize the object.
  __ str(R1, Address(R3, 0));
  __ AddImmediate(R0, kHeapObjectTag);

  // Initialize the tags.
  // R0: new object start as a tagged pointer.
  // R1: new object end address.
  // R2: allocation size.
  {
    const intptr_t shift = RawObject::kSizeTagBit - kObjectAlignmentLog2;
    const Class& cls =
        Class::Handle(isolate->object_store()->one_byte_string_class());

    __ CompareImmediate(R2, RawObject::SizeTag::kMaxSizeTag);
    __ mov(R2, ShifterOperand(R2, LSL, shift), LS);
    __ mov(R2, ShifterOperand(0), HI);

    // Get the class index and insert it into the tags.
    // R2: size and bit tags.
    __ LoadImmediate(TMP, RawObject::ClassIdTag::encode(cls.id()));
    __ orr(R2, R2, ShifterOperand(TMP));
    __ str(R2, FieldAddress(R0, String::tags_offset()));  // Store tags.
  }

  // Set the length field using the saved length (R6).
  __ StoreIntoObjectNoBarrier(R0,
                              FieldAddress(R0, String::length_offset()),
                              R6);
  // Clear hash.
  __ LoadImmediate(TMP, 0);
  __ str(TMP, FieldAddress(R0, String::hash_offset()));
  __ b(ok);

  __ Bind(&fail);
  __ b(failure);
}


// Arg0: Onebyte String
// Arg1: Start index as Smi.
// Arg2: End index as Smi.
// The indexes must be valid.
void Intrinsifier::OneByteString_substringUnchecked(Assembler* assembler) {
  const intptr_t kStringOffset = 2 * kWordSize;
  const intptr_t kStartIndexOffset = 1 * kWordSize;
  const intptr_t kEndIndexOffset = 0 * kWordSize;
  Label fall_through, ok;

  __ ldr(R2, Address(SP, kEndIndexOffset));
  __ ldr(TMP, Address(SP, kStartIndexOffset));
  __ sub(R2, R2, ShifterOperand(TMP));
  TryAllocateOnebyteString(assembler, &ok, &fall_through);
  __ Bind(&ok);
  // R0: new string as tagged pointer.
  // Copy string.
  __ ldr(R3, Address(SP, kStringOffset));
  __ ldr(R1, Address(SP, kStartIndexOffset));
  __ SmiUntag(R1);
  __ add(R3, R3, ShifterOperand(R1));
  // Calculate start address and untag (- 1).
  __ AddImmediate(R3, OneByteString::data_offset() - 1);

  // R3: Start address to copy from (untagged).
  // R1: Untagged start index.
  __ ldr(R2, Address(SP, kEndIndexOffset));
  __ SmiUntag(R2);
  __ sub(R2, R2, ShifterOperand(R1));

  // R3: Start address to copy from (untagged).
  // R2: Untagged number of bytes to copy.
  // R0: Tagged result string.
  // R6: Pointer into R3.
  // R7: Pointer into R0.
  // R1: Scratch register.
  Label loop, done;
  __ cmp(R2, ShifterOperand(0));
  __ b(&done, LE);
  __ mov(R6, ShifterOperand(R3));
  __ mov(R7, ShifterOperand(R0));
  __ Bind(&loop);
  __ ldrb(R1, Address(R6, 0));
  __ AddImmediate(R6, 1);
  __ sub(R2, R2, ShifterOperand(1));
  __ cmp(R2, ShifterOperand(0));
  __ strb(R1, FieldAddress(R7, OneByteString::data_offset()));
  __ AddImmediate(R7, 1);
  __ b(&loop, GT);

  __ Bind(&done);
  __ Ret();
  __ Bind(&fall_through);
}


void Intrinsifier::OneByteString_setAt(Assembler* assembler) {
  __ ldr(R2, Address(SP, 0 * kWordSize));  // Value.
  __ ldr(R1, Address(SP, 1 * kWordSize));  // Index.
  __ ldr(R0, Address(SP, 2 * kWordSize));  // OneByteString.
  __ SmiUntag(R1);
  __ SmiUntag(R2);
  __ AddImmediate(R3, R0, OneByteString::data_offset() - kHeapObjectTag);
  __ strb(R2, Address(R3, R1));
  __ Ret();
}


void Intrinsifier::OneByteString_allocate(Assembler* assembler) {
  __ ldr(R2, Address(SP, 0 * kWordSize));  // Length.
  Label fall_through, ok;
  TryAllocateOnebyteString(assembler, &ok, &fall_through);

  __ Bind(&ok);
  __ Ret();

  __ Bind(&fall_through);
}

}  // namespace dart

#endif  // defined TARGET_ARCH_ARM

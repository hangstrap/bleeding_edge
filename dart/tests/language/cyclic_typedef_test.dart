// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// Check that cyclic reference of a typedef is a compile-time error.

// To test various cyclic references the definition of the [:typedef A():] is
// split over several lines:
typedef

// Cyclic through return type.
A /// 01: compile-time error

A // The name of the typedef

( // The left parenthesis of the typedef arguments.

// Cyclic through parameter type.
A a /// 02: compile-time error

// Cyclic through optional parameter type.
[A a] /// 03: compile-time error

// Cyclic through named parameter type.
{A a} /// 04: compile-time error

// Cyclic through generic parameter type.
List<A> a /// 05: compile-time error

// Cyclic through return type of function typed parameter.
A f() /// 06: compile-time error

// Cyclic through parameter type of function typed parameter.
f(A a) /// 07: compile-time error

// Cyclic through another typedef.
B b /// 08: compile-time error

// Cyclic through another more typedefs.
C c /// 09: compile-time error

); // The right parenthesis of the typedef arguments.

typedef B(A a);
typedef C(B b);

void testA(A a) {}

void main() {
  testA(null);
}
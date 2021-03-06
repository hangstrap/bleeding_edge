// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library test.reflected_type_test;

import 'dart:mirrors';

import 'package:expect/expect.dart';

class A<T> {}
class B extends A {}            // Same as class B extends A<dynamic>.
class C extends A<num, int> {}  // Same as class C extends A<dynamic>.
class D extends A<int> {}
class E<S> extends A<S> {}
class F<R> extends A<int> {}
class G {}
class H<A,B,C> {}

expectReflectedType(classMirror, expectedType) {
  if (expectedType == null) {
    Expect.isFalse(classMirror.hasReflectedType);
    Expect.throws(() => classMirror.reflectedType,
                  (e) => e is UnsupportedError,
                  "Should not have a reflected type");
  } else {
    Expect.isTrue(classMirror.hasReflectedType);
    Expect.equals(expectedType, classMirror.reflectedType);
  }
}

main() {
  // Declarations.
  expectReflectedType(reflectClass(A), null);
  expectReflectedType(reflectClass(B), B);
  expectReflectedType(reflectClass(C), C);
  expectReflectedType(reflectClass(D), D);
  expectReflectedType(reflectClass(E), null);
  expectReflectedType(reflectClass(F), null);
  expectReflectedType(reflectClass(G), G);
  expectReflectedType(reflectClass(H), null);

   // Instantiations.
  expectReflectedType(reflect(new A()).type, new A().runtimeType);
  expectReflectedType(reflect(new B()).type, new B().runtimeType);
  expectReflectedType(reflect(new C()).type, new C().runtimeType);
  expectReflectedType(reflect(new D()).type, new D().runtimeType);
  expectReflectedType(reflect(new E()).type, new E().runtimeType);
  expectReflectedType(reflect(new F()).type, new F().runtimeType);
  expectReflectedType(reflect(new G()).type, new G().runtimeType);
  expectReflectedType(reflect(new H()).type, new H().runtimeType);

  expectReflectedType(reflect(new A<num>()).type, new A<num>().runtimeType);
  expectReflectedType(reflect(new B<num>()).type.superclass,
                      new A<dynamic>().runtimeType);
  expectReflectedType(reflect(new C<num>()).type.superclass,
                      new A<dynamic>().runtimeType);
  expectReflectedType(reflect(new D<num>()).type.superclass,
                      new A<int>().runtimeType);
  expectReflectedType(reflect(new E<num>()).type, new E<num>().runtimeType);
  // TODO(rmacnak): This requires some work to compute.
  // expectReflectedType(reflect(new E<num>()).type.superclass,
  //                     new A<num>().runtimeType);
  expectReflectedType(reflect(new F<num>()).type.superclass,
                      new A<int>().runtimeType);
  expectReflectedType(reflect(new F<num>()).type, new F<num>().runtimeType);
  expectReflectedType(reflect(new H<num, num, num>()).type,
                      new H<num, num, num>().runtimeType);
}

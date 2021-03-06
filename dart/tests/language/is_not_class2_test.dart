// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// Dart test program for catch that we expect a class after an 'is'. This is
// not a negative test since 'aa' is a malformed type and therefore should be
// treated as dynamic.

class A {
  const A();
}

class IsNotClass2NegativeTest {
  static testMain() {
    var a = new A();
    var aa = new A();

    if (a is aa) { // static warning
      return 0;
    }
    return 0;
  }
}

main() {
  IsNotClass2NegativeTest.testMain();
}

// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// This test uses the multi-test "ok" feature to create two positive tests from
// one file. One of these tests fail on dart2js, but pass on the VM, or vice
// versa.
// TODO(ahe): When both implementations agree, remove the multi-test parts.

library test.mixin_application_test;

import 'dart:mirrors';

import 'package:expect/expect.dart';

import 'model.dart';
import 'stringify.dart';

class Mixin {
  int i;
  m() {}
}

class Mixin2 {
  int i2;
  m2() {}
}

class MixinApplication = C with Mixin;
class MixinApplicationA = C with Mixin, Mixin2;

class UnusedMixinApplication = C with Mixin;

class Subclass extends C with Mixin {
  f() {}
}

class Subclass2 extends MixinApplication {
  g() {}
}

class SubclassA extends C with Mixin, Mixin2 {
  fa() {}
}

class Subclass2A extends MixinApplicationA {
  ga() {}
}

checkClass(Type type, List<String> expectedSuperclasses) {
  int i = 0;
  for (var cls = reflectClass(type); cls != null; cls = cls.superclass) {
    expect(expectedSuperclasses[i++], cls);
  }
  Expect.equals(i, expectedSuperclasses.length, '$type');
}

expectSame(ClassMirror a, ClassMirror b) {
  Expect.equals(a, b);
  expect(stringify(a), b);
  expect(stringify(b), a);
}

testMixin() {
  checkClass(Mixin, [
      'Class(s(Mixin) in s(test.mixin_application_test), top-level)',
      'Class(s(Object) in s(dart.core), top-level)',
  ]);

  expect(
      '{i: Variable(s(i) in s(Mixin)),'
      ' m: Method(s(m) in s(Mixin))}',
      reflectClass(Mixin).members);

  expect('{Mixin: Method(s(Mixin) in s(Mixin), constructor)}',
         reflectClass(Mixin).constructors);
}

testMixin2() {
  checkClass(Mixin2, [
      'Class(s(Mixin2) in s(test.mixin_application_test), top-level)',
      'Class(s(Object) in s(dart.core), top-level)',
  ]);

  expect(
      '{i2: Variable(s(i2) in s(Mixin2)),'
      ' m2: Method(s(m2) in s(Mixin2))}',
      reflectClass(Mixin2).members);

  expect('{Mixin2: Method(s(Mixin2) in s(Mixin2), constructor)}',
         reflectClass(Mixin2).constructors);
}

testMixinApplication() {
  checkClass(MixinApplication, [
      'Class(s(MixinApplication) in s(test.mixin_application_test), top-level)',
      'Class(s(C) in s(test.model), top-level)',
      'Class(s(B) in s(test.model), top-level)',
      'Class(s(A) in s(test.model), top-level)',
      'Class(s(Object) in s(dart.core), top-level)',
  ]);

  String owner = 'Mixin';
  owner = 'MixinApplication'; /// 01: ok
  expect(
      '{i: Variable(s(i) in s($owner)),'
      ' m: Method(s(m) in s($owner))}',
      reflectClass(MixinApplication).members);

  expect('{MixinApplication: Method(s(MixinApplication) in s(MixinApplication),'
         ' constructor)}',
         reflectClass(MixinApplication).constructors);

  expectSame(reflectClass(C), reflectClass(MixinApplication).superclass);
}

testMixinApplicationA() {
  // TODO(ahe): I don't think an anonymous mixin has an owner.
  String owner = ' in s(test.mixin_application_test)';
  owner = ''; /// 01: ok
  checkClass(MixinApplicationA, [
      'Class(s(MixinApplicationA)'
      ' in s(test.mixin_application_test), top-level)',
      'Class(s(test.model.C with test.mixin_application_test.Mixin)'
      '$owner, top-level)',
      'Class(s(C) in s(test.model), top-level)',
      'Class(s(B) in s(test.model), top-level)',
      'Class(s(A) in s(test.model), top-level)',
      'Class(s(Object) in s(dart.core), top-level)',
  ]);

  owner = 'Mixin2';
  owner = 'MixinApplicationA'; /// 01: ok
  expect(
      '{i2: Variable(s(i2) in s($owner)),'
      ' m2: Method(s(m2) in s($owner))}',
      reflectClass(MixinApplicationA).members);

  expect(
      '{MixinApplicationA: Method(s(MixinApplicationA) in s(MixinApplicationA),'
      ' constructor)}',
      reflectClass(MixinApplicationA).constructors);

  expect(
      '{i: Variable(s(i) in s(Mixin)),'
      ' m: Method(s(m) in s(Mixin))}',
      reflectClass(MixinApplicationA).superclass.members);

  String name = 'test.model.C with test.mixin_application_test.Mixin';
  name = 'Mixin'; /// 01: ok
  expect(
      '{$name:'
      ' Method(s($name)'
      ' in s($name), constructor)}',
      reflectClass(MixinApplicationA).superclass.constructors);

  expectSame(
      reflectClass(C),
      reflectClass(MixinApplicationA).superclass.superclass);
}

testUnusedMixinApplication() {
  checkClass(UnusedMixinApplication, [
      'Class(s(UnusedMixinApplication) in s(test.mixin_application_test),'
      ' top-level)',
      'Class(s(C) in s(test.model), top-level)',
      'Class(s(B) in s(test.model), top-level)',
      'Class(s(A) in s(test.model), top-level)',
      'Class(s(Object) in s(dart.core), top-level)',
  ]);

  String owner = 'Mixin';
  owner = 'UnusedMixinApplication'; /// 01: ok
  expect(
      '{i: Variable(s(i) in s($owner)),'
      ' m: Method(s(m) in s($owner))}',
      reflectClass(UnusedMixinApplication).members);

  expect(
      '{UnusedMixinApplication: Method(s(UnusedMixinApplication)'
      ' in s(UnusedMixinApplication), constructor)}',
      reflectClass(UnusedMixinApplication).constructors);

  expectSame(reflectClass(C), reflectClass(UnusedMixinApplication).superclass);
}

testSubclass() {
  String owner = ' in s(test.mixin_application_test)';
  owner = ''; /// 01: ok
  checkClass(Subclass, [
      'Class(s(Subclass) in s(test.mixin_application_test), top-level)',
      'Class(s(test.model.C with test.mixin_application_test.Mixin)'
      '$owner, top-level)',
      'Class(s(C) in s(test.model), top-level)',
      'Class(s(B) in s(test.model), top-level)',
      'Class(s(A) in s(test.model), top-level)',
      'Class(s(Object) in s(dart.core), top-level)',
  ]);

  expect(
      '{f: Method(s(f) in s(Subclass))}',
      reflectClass(Subclass).members);

  expect(
      '{Subclass: Method(s(Subclass) in s(Subclass), constructor)}',
      reflectClass(Subclass).constructors);

  expect(
      '{i: Variable(s(i) in s(Mixin)),'
      ' m: Method(s(m) in s(Mixin))}',
      reflectClass(Subclass).superclass.members);

  String name = 'test.model.C with test.mixin_application_test.Mixin';
  name = 'Mixin'; /// 01: ok
  expect(
      '{$name:'
      ' Method(s($name)'
      ' in s($name), constructor)}',
      reflectClass(Subclass).superclass.constructors);

  expectSame(
      reflectClass(C),
      reflectClass(Subclass).superclass.superclass);
}

testSubclass2() {
  checkClass(Subclass2, [
      'Class(s(Subclass2) in s(test.mixin_application_test), top-level)',
      'Class(s(MixinApplication) in s(test.mixin_application_test), top-level)',
      'Class(s(C) in s(test.model), top-level)',
      'Class(s(B) in s(test.model), top-level)',
      'Class(s(A) in s(test.model), top-level)',
      'Class(s(Object) in s(dart.core), top-level)',
  ]);

  expect(
      '{g: Method(s(g) in s(Subclass2))}',
      reflectClass(Subclass2).members);

  expect(
      '{Subclass2: Method(s(Subclass2) in s(Subclass2), constructor)}',
      reflectClass(Subclass2).constructors);

  expectSame(
      reflectClass(MixinApplication),
      reflectClass(Subclass2).superclass);
}

testSubclassA() {
  // TODO(ahe): I don't think an anonymous mixin has an owner.
  String owner = ' in s(test.mixin_application_test)';
  owner = ''; /// 01: ok
  checkClass(SubclassA, [
      'Class(s(SubclassA) in s(test.mixin_application_test), top-level)',
      'Class(s(test.model.C with test.mixin_application_test.Mixin,'
      ' test.mixin_application_test.Mixin2)$owner, top-level)',
      'Class(s(test.model.C with test.mixin_application_test.Mixin)$owner,'
      ' top-level)',
      'Class(s(C) in s(test.model), top-level)',
      'Class(s(B) in s(test.model), top-level)',
      'Class(s(A) in s(test.model), top-level)',
      'Class(s(Object) in s(dart.core), top-level)',
  ]);

  expect(
      '{fa: Method(s(fa) in s(SubclassA))}',
      reflectClass(SubclassA).members);

  expect(
      '{SubclassA: Method(s(SubclassA) in s(SubclassA), constructor)}',
      reflectClass(SubclassA).constructors);

  expect(
      '{i2: Variable(s(i2) in s(Mixin2)),'
      ' m2: Method(s(m2) in s(Mixin2))}',
      reflectClass(SubclassA).superclass.members);

  String name =
      'test.model.C with test.mixin_application_test.Mixin,'
      ' test.mixin_application_test.Mixin2';
  name = 'Mixin2'; /// 01: ok
  expect(
      '{$name: Method(s($name) in s($name), constructor)}',
      reflectClass(SubclassA).superclass.constructors);

  expect(
      '{i: Variable(s(i) in s(Mixin)),'
      ' m: Method(s(m) in s(Mixin))}',
      reflectClass(SubclassA).superclass.superclass.members);

  name = 'test.model.C with test.mixin_application_test.Mixin';
  name = 'Mixin'; /// 01: ok
  expect(
      '{$name:'
      ' Method(s($name)'
      ' in s($name), constructor)}',
      reflectClass(SubclassA).superclass.superclass.constructors);

  expectSame(
      reflectClass(C),
      reflectClass(SubclassA).superclass.superclass.superclass);
}

testSubclass2A() {
  // TODO(ahe): I don't think an anonymous mixin has an owner.
  String owner = ' in s(test.mixin_application_test)';
  owner = ''; /// 01: ok
  checkClass(Subclass2A, [
      'Class(s(Subclass2A) in s(test.mixin_application_test), top-level)',
      'Class(s(MixinApplicationA) in s(test.mixin_application_test),'
      ' top-level)',
      'Class(s(test.model.C with test.mixin_application_test.Mixin)$owner,'
      ' top-level)',
      'Class(s(C) in s(test.model), top-level)',
      'Class(s(B) in s(test.model), top-level)',
      'Class(s(A) in s(test.model), top-level)',
      'Class(s(Object) in s(dart.core), top-level)',
  ]);

  expect(
      '{ga: Method(s(ga) in s(Subclass2A))}',
      reflectClass(Subclass2A).members);

  expect(
      '{Subclass2A: Method(s(Subclass2A) in s(Subclass2A), constructor)}',
      reflectClass(Subclass2A).constructors);

  expectSame(reflectClass(MixinApplicationA),
             reflectClass(Subclass2A).superclass);
}

main() {
  testMixin();
  testMixin2();
  testMixinApplication();
  testMixinApplicationA();
  testUnusedMixinApplication();
  testSubclass();
  testSubclass2();
  testSubclassA();
  testSubclass2A();
}

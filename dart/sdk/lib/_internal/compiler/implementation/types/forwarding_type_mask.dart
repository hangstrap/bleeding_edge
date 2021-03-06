// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

part of types;

/**
 * A type mask that wraps an other one, and delecate all its
 * implementation methods to it.
 */
abstract class ForwardingTypeMask implements TypeMask {

  TypeMask get forwardTo;

  ForwardingTypeMask();

  bool get isEmpty => forwardTo.isEmpty;
  bool get isNullable => forwardTo.isNullable;
  bool get isExact => forwardTo.isExact;

  bool get isUnion => false;
  bool get isContainer => false;
  bool get isForwarding => true;

  bool containsOnlyInt(Compiler compiler) {
    return forwardTo.containsOnlyInt(compiler);
  }

  bool containsOnlyDouble(Compiler compiler) {
    return forwardTo.containsOnlyDouble(compiler);
  }

  bool containsOnlyNum(Compiler compiler) {
    return forwardTo.containsOnlyNum(compiler);
  }

  bool containsOnlyNull(Compiler compiler) {
    return forwardTo.containsOnlyNull(compiler);
  }

  bool containsOnlyBool(Compiler compiler) {
    return forwardTo.containsOnlyBool(compiler);
  }

  bool containsOnlyString(Compiler compiler) {
    return forwardTo.containsOnlyString(compiler);
  }

  bool containsOnly(ClassElement element) {
    return forwardTo.containsOnly(element);
  }

  bool satisfies(ClassElement cls, Compiler compiler) {
    return forwardTo.satisfies(cls, compiler);
  }

  bool contains(ClassElement type, Compiler compiler) {
    return forwardTo.contains(type, compiler);
  }

  bool containsAll(Compiler compiler) {
    return forwardTo.containsAll(compiler);
  }

  ClassElement singleClass(Compiler compiler) {
    return forwardTo.singleClass(compiler);
  }

  Iterable<ClassElement> containedClasses(Compiler compiler) {
    return forwardTo.containedClasses(compiler);
  }

  TypeMask union(other, Compiler compiler) {
    if (this == other) {
      return this;
    } else if (equalsDisregardNull(other)) {
      return other.isNullable ? other : this;
    } else if (other.isEmpty) {
      return other.isNullable ? this.nullable() : this;
    }
    return forwardTo.union(other, compiler);
  }

  TypeMask intersection(TypeMask other, Compiler compiler) {
    return forwardTo.intersection(other, compiler);
  }

  bool understands(Selector selector, Compiler compiler) {
    return forwardTo.understands(selector, compiler);
  }

  bool needsNoSuchMethodHandling(Selector selector, Compiler compiler) {
    return forwardTo.needsNoSuchMethodHandling(selector, compiler);
  }

  bool canHit(Element element, Selector selector, Compiler compiler) {
    return forwardTo.canHit(element, selector, compiler);
  }

  Element locateSingleElement(Selector selector, Compiler compiler) {
    return forwardTo.locateSingleElement(selector, compiler);
  }

  TypeMask simplify(Compiler compiler) => forwardTo.simplify(compiler);

  bool equalsDisregardNull(other);
}

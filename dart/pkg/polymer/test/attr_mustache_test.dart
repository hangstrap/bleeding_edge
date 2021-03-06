// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'dart:async';
import 'dart:html';
import 'package:unittest/unittest.dart';
import 'package:unittest/html_config.dart';
import 'package:polymer/polymer.dart';

@CustomTag('x-target')
class XTarget extends PolymerElement {
  XTarget.created() : super.created();

  final Completer _found = new Completer();
  Future get foundSrc => _found.future;

  // force an mdv binding
  bind(name, model, path) =>
      TemplateElement.mdvPackage(this).bind(name, model, path);

  inserted() {
    testSrcForMustache();
  }

  attributeChanged(name, oldValue, newValue) {
    testSrcForMustache();
    if (attributes[name] == '../testSource') {
      _found.complete();
    }
  }

  testSrcForMustache() {
    expect(attributes['src'], isNot(matches(Polymer.bindPattern)),
        reason: 'attribute does not contain {{...}}');
  }
}

@CustomTag('x-test')
class XTest extends PolymerElement {
  XTest.created() : super.created();

  @observable var src = 'testSource';
}

@initMethod _main() {
  useHtmlConfiguration();

  test('mustache attributes', () {
    final xtest = document.query('#test').xtag;
    final xtarget = xtest.shadowRoot.query('#target').xtag;
    return xtarget.foundSrc;
  });
}

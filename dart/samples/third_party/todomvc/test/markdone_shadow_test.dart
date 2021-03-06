// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library todomvc.test.markdone_shadow_test;

// We don't use this import, but it signals to the test framework that this is
// a web test.
import 'dart:html';
import 'package:polymer/polymer.dart' show initMethod;
import 'markdone_test.dart' as markdone_test;

@initMethod _main() {
  // Make analyzer happy by using dart:html.
  document.query('body');
  markdone_test.init();
}

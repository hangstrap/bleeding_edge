// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library polymer.test.build.polyfill_injector_test;

import 'package:polymer/src/build/common.dart';
import 'package:polymer/src/build/polyfill_injector.dart';
import 'package:unittest/compact_vm_config.dart';

import 'common.dart';

void main() {
  useCompactVMConfiguration();
  var phases = [[new PolyfillInjector(new TransformOptions())]];

  testPhases('no changes', phases, {
      'a|web/test.html': '<!DOCTYPE html><html></html>',
    }, {
      'a|web/test.html': '<!DOCTYPE html><html></html>',
    });

  testPhases('no changes under lib ', phases, {
      'a|lib/test.html':
          '<!DOCTYPE html><html><head></head><body>'
          '<script type="application/dart" src="a.dart"></script>',
    }, {
      'a|lib/test.html':
          '<!DOCTYPE html><html><head></head><body>'
          '<script type="application/dart" src="a.dart"></script>',
    });

  testPhases('with some script', phases, {
      'a|web/test.html':
          '<!DOCTYPE html><html><head></head><body>'
          '<script type="application/dart" src="a.dart"></script>',
    }, {
      'a|web/test.html':
          '<!DOCTYPE html><html><head></head><body>'
          '$SHADOW_DOM_TAG$CUSTOM_ELEMENT_TAG$INTEROP_TAG'
          '<script type="application/dart" src="a.dart"></script>'
          '</body></html>',
    });

  testPhases('interop/shadow dom already present', phases, {
      'a|web/test.html':
          '<!DOCTYPE html><html><head></head><body>'
          '<script type="application/dart" src="a.dart"></script>'
          '$SHADOW_DOM_TAG'
          '$CUSTOM_ELEMENT_TAG'
          '$INTEROP_TAG'
    }, {
      'a|web/test.html':
          '<!DOCTYPE html><html><head></head><body>'
          '<script type="application/dart" src="a.dart"></script>'
          '$SHADOW_DOM_TAG'
          '$CUSTOM_ELEMENT_TAG'
          '$INTEROP_TAG'
          '</body></html>',
    });
}

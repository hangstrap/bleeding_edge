// Copyright (c) 2013, the Dart project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// This test verifies that failing secure connection attempts always complete
// their returned future.

import "package:expect/expect.dart";
import "package:path/path.dart";
import "dart:async";
import "dart:io";

const HOST_NAME = "localhost";
const CERTIFICATE = "localhost_cert";

Future<SecureServerSocket> runServer() {
  SecureSocket.initialize(
      database: join(dirname(new Options().script), 'pkcert'),
      password: 'dartdart');

  return SecureServerSocket.bind(HOST_NAME, 0, CERTIFICATE)
      .then((SecureServerSocket server) {
    server.listen((SecureSocket socket) {
      socket.listen((_) { },
                    onDone: () {
                      socket.close();
                    });
    }, onError: (e) => Expect.isTrue(e is HandshakeException));
    return server;
  });
}

void main() {
  final options = new Options();
  var clientScript = join(dirname(options.script),
                          'secure_unauthorized_client.dart');

  Future clientProcess(int port) {
    return Process.run(options.executable,
        [clientScript, port.toString()])
    .then((ProcessResult result) {
      if (result.exitCode != 0 || !result.stdout.contains('SUCCESS')) {
        print("Client failed");
        print("  stdout:");
        print(result.stdout);
        print("  stderr:");
        print(result.stderr);
        Expect.fail('Client subprocess exit code: ${result.exitCode}');
      }
    });
  }

  runServer().then((server) {
    clientProcess(server.port).then((_) {
      server.close();
    });
  });
}

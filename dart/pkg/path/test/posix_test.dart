// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library path.test.posix_test;

import 'package:unittest/unittest.dart';
import 'package:path/path.dart' as path;

main() {
  var builder = new path.Builder(style: path.Style.posix, root: '/root/path');

  if (new path.Builder().style == path.Style.posix) {
    group('absolute', () {
      expect(path.absolute('a/b.txt'), path.join(path.current, 'a/b.txt'));
      expect(path.absolute('/a/b.txt'), '/a/b.txt');
    });
  }

  test('separator', () {
    expect(builder.separator, '/');
  });

  test('extension', () {
    expect(builder.extension(''), '');
    expect(builder.extension('.'), '');
    expect(builder.extension('..'), '');
    expect(builder.extension('foo.dart'), '.dart');
    expect(builder.extension('foo.dart.js'), '.js');
    expect(builder.extension('a.b/c'), '');
    expect(builder.extension('a.b/c.d'), '.d');
    expect(builder.extension('~/.bashrc'), '');
    expect(builder.extension(r'a.b\c'), r'.b\c');
    expect(builder.extension('foo.dart/'), '.dart');
    expect(builder.extension('foo.dart//'), '.dart');
  });

  test('rootPrefix', () {
    expect(builder.rootPrefix(''), '');
    expect(builder.rootPrefix('a'), '');
    expect(builder.rootPrefix('a/b'), '');
    expect(builder.rootPrefix('/a/c'), '/');
    expect(builder.rootPrefix('/'), '/');
  });

  test('dirname', () {
    expect(builder.dirname(''), '.');
    expect(builder.dirname('.'), '.');
    expect(builder.dirname('..'), '.');
    expect(builder.dirname('../..'), '..');
    expect(builder.dirname('a'), '.');
    expect(builder.dirname('a/b'), 'a');
    expect(builder.dirname('a/b/c'), 'a/b');
    expect(builder.dirname('a/b.c'), 'a');
    expect(builder.dirname('a/'), '.');
    expect(builder.dirname('a/.'), 'a');
    expect(builder.dirname('a/..'), 'a');
    expect(builder.dirname(r'a\b/c'), r'a\b');
    expect(builder.dirname('/a'), '/');
    expect(builder.dirname('///a'), '/');
    expect(builder.dirname('/'), '/');
    expect(builder.dirname('///'), '/');
    expect(builder.dirname('a/b/'), 'a');
    expect(builder.dirname(r'a/b\c'), 'a');
    expect(builder.dirname('a//'), '.');
    expect(builder.dirname('a/b//'), 'a');
    expect(builder.dirname('a//b'), 'a');
  });

  test('basename', () {
    expect(builder.basename(''), '');
    expect(builder.basename('.'), '.');
    expect(builder.basename('..'), '..');
    expect(builder.basename('.foo'), '.foo');
    expect(builder.basename('a'), 'a');
    expect(builder.basename('a/b'), 'b');
    expect(builder.basename('a/b/c'), 'c');
    expect(builder.basename('a/b.c'), 'b.c');
    expect(builder.basename('a/'), 'a');
    expect(builder.basename('a/.'), '.');
    expect(builder.basename('a/..'), '..');
    expect(builder.basename(r'a\b/c'), 'c');
    expect(builder.basename('/a'), 'a');
    expect(builder.basename('/'), '/');
    expect(builder.basename('a/b/'), 'b');
    expect(builder.basename(r'a/b\c'), r'b\c');
    expect(builder.basename('a//'), 'a');
    expect(builder.basename('a/b//'), 'b');
    expect(builder.basename('a//b'), 'b');
  });

  test('basenameWithoutExtension', () {
    expect(builder.basenameWithoutExtension(''), '');
    expect(builder.basenameWithoutExtension('.'), '.');
    expect(builder.basenameWithoutExtension('..'), '..');
    expect(builder.basenameWithoutExtension('a'), 'a');
    expect(builder.basenameWithoutExtension('a/b'), 'b');
    expect(builder.basenameWithoutExtension('a/b/c'), 'c');
    expect(builder.basenameWithoutExtension('a/b.c'), 'b');
    expect(builder.basenameWithoutExtension('a/'), 'a');
    expect(builder.basenameWithoutExtension('a/.'), '.');
    expect(builder.basenameWithoutExtension(r'a/b\c'), r'b\c');
    expect(builder.basenameWithoutExtension('a/.bashrc'), '.bashrc');
    expect(builder.basenameWithoutExtension('a/b/c.d.e'), 'c.d');
    expect(builder.basenameWithoutExtension('a//'), 'a');
    expect(builder.basenameWithoutExtension('a/b//'), 'b');
    expect(builder.basenameWithoutExtension('a//b'), 'b');
    expect(builder.basenameWithoutExtension('a/b.c/'), 'b');
    expect(builder.basenameWithoutExtension('a/b.c//'), 'b');
    expect(builder.basenameWithoutExtension('a/b c.d e'), 'b c');
  });

  test('isAbsolute', () {
    expect(builder.isAbsolute(''), false);
    expect(builder.isAbsolute('a'), false);
    expect(builder.isAbsolute('a/b'), false);
    expect(builder.isAbsolute('/a'), true);
    expect(builder.isAbsolute('/a/b'), true);
    expect(builder.isAbsolute('~'), false);
    expect(builder.isAbsolute('.'), false);
    expect(builder.isAbsolute('..'), false);
    expect(builder.isAbsolute('.foo'), false);
    expect(builder.isAbsolute('../a'), false);
    expect(builder.isAbsolute('C:/a'), false);
    expect(builder.isAbsolute(r'C:\a'), false);
    expect(builder.isAbsolute(r'\\a'), false);
  });

  test('isRelative', () {
    expect(builder.isRelative(''), true);
    expect(builder.isRelative('a'), true);
    expect(builder.isRelative('a/b'), true);
    expect(builder.isRelative('/a'), false);
    expect(builder.isRelative('/a/b'), false);
    expect(builder.isRelative('~'), true);
    expect(builder.isRelative('.'), true);
    expect(builder.isRelative('..'), true);
    expect(builder.isRelative('.foo'), true);
    expect(builder.isRelative('../a'), true);
    expect(builder.isRelative('C:/a'), true);
    expect(builder.isRelative(r'C:\a'), true);
    expect(builder.isRelative(r'\\a'), true);
  });

  group('join', () {
    test('allows up to eight parts', () {
      expect(builder.join('a'), 'a');
      expect(builder.join('a', 'b'), 'a/b');
      expect(builder.join('a', 'b', 'c'), 'a/b/c');
      expect(builder.join('a', 'b', 'c', 'd'), 'a/b/c/d');
      expect(builder.join('a', 'b', 'c', 'd', 'e'), 'a/b/c/d/e');
      expect(builder.join('a', 'b', 'c', 'd', 'e', 'f'), 'a/b/c/d/e/f');
      expect(builder.join('a', 'b', 'c', 'd', 'e', 'f', 'g'), 'a/b/c/d/e/f/g');
      expect(builder.join('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'),
          'a/b/c/d/e/f/g/h');
    });

    test('does not add separator if a part ends in one', () {
      expect(builder.join('a/', 'b', 'c/', 'd'), 'a/b/c/d');
      expect(builder.join('a\\', 'b'), r'a\/b');
    });

    test('ignores parts before an absolute path', () {
      expect(builder.join('a', '/', 'b', 'c'), '/b/c');
      expect(builder.join('a', '/b', '/c', 'd'), '/c/d');
      expect(builder.join('a', r'c:\b', 'c', 'd'), r'a/c:\b/c/d');
      expect(builder.join('a', r'\\b', 'c', 'd'), r'a/\\b/c/d');
    });

    test('ignores trailing nulls', () {
      expect(builder.join('a', null), equals('a'));
      expect(builder.join('a', 'b', 'c', null, null), equals('a/b/c'));
    });

    test('ignores empty strings', () {
      expect(builder.join(''), '');
      expect(builder.join('', ''), '');
      expect(builder.join('', 'a'), 'a');
      expect(builder.join('a', '', 'b', '', '', '', 'c'), 'a/b/c');
      expect(builder.join('a', 'b', ''), 'a/b');
    });

    test('disallows intermediate nulls', () {
      expect(() => builder.join('a', null, 'b'), throwsArgumentError);
      expect(() => builder.join(null, 'a'), throwsArgumentError);
    });

    test('join does not modify internal ., .., or trailing separators', () {
      expect(builder.join('a/', 'b/c/'), 'a/b/c/');
      expect(builder.join('a/b/./c/..//', 'd/.././..//e/f//'),
             'a/b/./c/..//d/.././..//e/f//');
      expect(builder.join('a/b', 'c/../../../..'), 'a/b/c/../../../..');
      expect(builder.join('a', 'b${builder.separator}'), 'a/b/');
    });
  });

  group('joinAll', () {
    test('allows more than eight parts', () {
      expect(builder.joinAll(['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i']),
          'a/b/c/d/e/f/g/h/i');
    });

    test('does not add separator if a part ends in one', () {
      expect(builder.joinAll(['a/', 'b', 'c/', 'd']), 'a/b/c/d');
      expect(builder.joinAll(['a\\', 'b']), r'a\/b');
    });

    test('ignores parts before an absolute path', () {
      expect(builder.joinAll(['a', '/', 'b', 'c']), '/b/c');
      expect(builder.joinAll(['a', '/b', '/c', 'd']), '/c/d');
      expect(builder.joinAll(['a', r'c:\b', 'c', 'd']), r'a/c:\b/c/d');
      expect(builder.joinAll(['a', r'\\b', 'c', 'd']), r'a/\\b/c/d');
    });
  });

  group('split', () {
    test('simple cases', () {
      expect(builder.split(''), []);
      expect(builder.split('.'), ['.']);
      expect(builder.split('..'), ['..']);
      expect(builder.split('foo'), equals(['foo']));
      expect(builder.split('foo/bar.txt'), equals(['foo', 'bar.txt']));
      expect(builder.split('foo/bar/baz'), equals(['foo', 'bar', 'baz']));
      expect(builder.split('foo/../bar/./baz'),
          equals(['foo', '..', 'bar', '.', 'baz']));
      expect(builder.split('foo//bar///baz'), equals(['foo', 'bar', 'baz']));
      expect(builder.split('foo/\\/baz'), equals(['foo', '\\', 'baz']));
      expect(builder.split('.'), equals(['.']));
      expect(builder.split(''), equals([]));
      expect(builder.split('foo/'), equals(['foo']));
      expect(builder.split('//'), equals(['/']));
    });

    test('includes the root for absolute paths', () {
      expect(builder.split('/foo/bar/baz'), equals(['/', 'foo', 'bar', 'baz']));
      expect(builder.split('/'), equals(['/']));
    });
  });

  group('normalize', () {
    test('simple cases', () {
      expect(builder.normalize(''), '.');
      expect(builder.normalize('.'), '.');
      expect(builder.normalize('..'), '..');
      expect(builder.normalize('a'), 'a');
      expect(builder.normalize('/'), '/');
      expect(builder.normalize(r'\'), r'\');
      expect(builder.normalize('C:/'), 'C:');
      expect(builder.normalize(r'C:\'), r'C:\');
      expect(builder.normalize(r'\\'), r'\\');
      expect(builder.normalize('a/./\xc5\u0bf8-;\u{1f085}\u{00}/c/d/../'),
             'a/\xc5\u0bf8-;\u{1f085}\u{00}/c');
    });

    test('collapses redundant separators', () {
      expect(builder.normalize(r'a/b/c'), r'a/b/c');
      expect(builder.normalize(r'a//b///c////d'), r'a/b/c/d');
    });

    test('does not collapse separators for other platform', () {
      expect(builder.normalize(r'a\\b\\\c'), r'a\\b\\\c');
    });

    test('eliminates "." parts', () {
      expect(builder.normalize('./'), '.');
      expect(builder.normalize('/.'), '/');
      expect(builder.normalize('/./'), '/');
      expect(builder.normalize('./.'), '.');
      expect(builder.normalize('a/./b'), 'a/b');
      expect(builder.normalize('a/.b/c'), 'a/.b/c');
      expect(builder.normalize('a/././b/./c'), 'a/b/c');
      expect(builder.normalize('././a'), 'a');
      expect(builder.normalize('a/./.'), 'a');
    });

    test('eliminates ".." parts', () {
      expect(builder.normalize('..'), '..');
      expect(builder.normalize('../'), '..');
      expect(builder.normalize('../../..'), '../../..');
      expect(builder.normalize('../../../'), '../../..');
      expect(builder.normalize('/..'), '/');
      expect(builder.normalize('/../../..'), '/');
      expect(builder.normalize('/../../../a'), '/a');
      expect(builder.normalize('c:/..'), '.');
      expect(builder.normalize('A:/../../..'), '../..');
      expect(builder.normalize('a/..'), '.');
      expect(builder.normalize('a/b/..'), 'a');
      expect(builder.normalize('a/../b'), 'b');
      expect(builder.normalize('a/./../b'), 'b');
      expect(builder.normalize('a/b/c/../../d/e/..'), 'a/d');
      expect(builder.normalize('a/b/../../../../c'), '../../c');
      expect(builder.normalize(r'z/a/b/../../..\../c'), r'z/..\../c');
      expect(builder.normalize(r'a/b\c/../d'), 'a/d');
    });

    test('does not walk before root on absolute paths', () {
      expect(builder.normalize('..'), '..');
      expect(builder.normalize('../'), '..');
      expect(builder.normalize('http://dartlang.org/..'), 'http:');
      expect(builder.normalize('http://dartlang.org/../../a'), 'a');
      expect(builder.normalize('file:///..'), '.');
      expect(builder.normalize('file:///../../a'), '../a');
      expect(builder.normalize('/..'), '/');
      expect(builder.normalize('a/..'), '.');
      expect(builder.normalize('../a'), '../a');
      expect(builder.normalize('/../a'), '/a');
      expect(builder.normalize('c:/../a'), 'a');
      expect(builder.normalize('/../a'), '/a');
      expect(builder.normalize('a/b/..'), 'a');
      expect(builder.normalize('../a/b/..'), '../a');
      expect(builder.normalize('a/../b'), 'b');
      expect(builder.normalize('a/./../b'), 'b');
      expect(builder.normalize('a/b/c/../../d/e/..'), 'a/d');
      expect(builder.normalize('a/b/../../../../c'), '../../c');
      expect(builder.normalize('a/b/c/../../..d/./.e/f././'), 'a/..d/.e/f.');
    });

    test('removes trailing separators', () {
      expect(builder.normalize('./'), '.');
      expect(builder.normalize('.//'), '.');
      expect(builder.normalize('a/'), 'a');
      expect(builder.normalize('a/b/'), 'a/b');
      expect(builder.normalize(r'a/b\'), r'a/b\');
      expect(builder.normalize('a/b///'), 'a/b');
    });
  });

  group('relative', () {
    group('from absolute root', () {
      test('given absolute path in root', () {
        expect(builder.relative('/'), '../..');
        expect(builder.relative('/root'), '..');
        expect(builder.relative('/root/path'), '.');
        expect(builder.relative('/root/path/a'), 'a');
        expect(builder.relative('/root/path/a/b.txt'), 'a/b.txt');
        expect(builder.relative('/root/a/b.txt'), '../a/b.txt');
      });

      test('given absolute path outside of root', () {
        expect(builder.relative('/a/b'), '../../a/b');
        expect(builder.relative('/root/path/a'), 'a');
        expect(builder.relative('/root/path/a/b.txt'), 'a/b.txt');
        expect(builder.relative('/root/a/b.txt'), '../a/b.txt');
      });

      test('given relative path', () {
        // The path is considered relative to the root, so it basically just
        // normalizes.
        expect(builder.relative(''), '.');
        expect(builder.relative('.'), '.');
        expect(builder.relative('a'), 'a');
        expect(builder.relative('a/b.txt'), 'a/b.txt');
        expect(builder.relative('../a/b.txt'), '../a/b.txt');
        expect(builder.relative('a/./b/../c.txt'), 'a/c.txt');
      });

      // Regression
      test('from root-only path', () {
        expect(builder.relative('/', from: '/'), '.');
        expect(builder.relative('/root/path', from: '/'), 'root/path');
      });
    });

    group('from relative root', () {
      var r = new path.Builder(style: path.Style.posix, root: 'foo/bar');

      test('given absolute path', () {
        expect(r.relative('/'), equals('/'));
        expect(r.relative('/a/b'), equals('/a/b'));
      });

      test('given relative path', () {
        // The path is considered relative to the root, so it basically just
        // normalizes.
        expect(r.relative(''), '.');
        expect(r.relative('.'), '.');
        expect(r.relative('..'), '..');
        expect(r.relative('a'), 'a');
        expect(r.relative('a/b.txt'), 'a/b.txt');
        expect(r.relative('../a/b.txt'), '../a/b.txt');
        expect(r.relative('a/./b/../c.txt'), 'a/c.txt');
      });
    });

    test('from a root with extension', () {
      var r = new path.Builder(style: path.Style.posix, root: '/dir.ext');
      expect(r.relative('/dir.ext/file'), 'file');
    });

    test('with a root parameter', () {
      expect(builder.relative('/foo/bar/baz', from: '/foo/bar'), equals('baz'));
      expect(builder.relative('..', from: '/foo/bar'), equals('../../root'));
      expect(builder.relative('/foo/bar/baz', from: 'foo/bar'),
          equals('../../../../foo/bar/baz'));
      expect(builder.relative('..', from: 'foo/bar'), equals('../../..'));
    });

    test('with a root parameter and a relative root', () {
      var r = new path.Builder(style: path.Style.posix, root: 'relative/root');
      expect(r.relative('/foo/bar/baz', from: '/foo/bar'), equals('baz'));
      expect(() => r.relative('..', from: '/foo/bar'), throwsArgumentError);
      expect(r.relative('/foo/bar/baz', from: 'foo/bar'),
          equals('/foo/bar/baz'));
      expect(r.relative('..', from: 'foo/bar'), equals('../../..'));
    });

    test('from a . root', () {
      var r = new path.Builder(style: path.Style.posix, root: '.');
      expect(r.relative('/foo/bar/baz'), equals('/foo/bar/baz'));
      expect(r.relative('foo/bar/baz'), equals('foo/bar/baz'));
    });
  });

  group('resolve', () {
    test('allows up to seven parts', () {
      expect(builder.resolve('a'), '/root/path/a');
      expect(builder.resolve('a', 'b'), '/root/path/a/b');
      expect(builder.resolve('a', 'b', 'c'), '/root/path/a/b/c');
      expect(builder.resolve('a', 'b', 'c', 'd'), '/root/path/a/b/c/d');
      expect(builder.resolve('a', 'b', 'c', 'd', 'e'), '/root/path/a/b/c/d/e');
      expect(builder.resolve('a', 'b', 'c', 'd', 'e', 'f'),
          '/root/path/a/b/c/d/e/f');
      expect(builder.resolve('a', 'b', 'c', 'd', 'e', 'f', 'g'),
          '/root/path/a/b/c/d/e/f/g');
    });

    test('does not add separator if a part ends in one', () {
      expect(builder.resolve('a/', 'b', 'c/', 'd'), '/root/path/a/b/c/d');
      expect(builder.resolve(r'a\', 'b'), r'/root/path/a\/b');
    });

    test('ignores parts before an absolute path', () {
      expect(builder.resolve('a', '/b', '/c', 'd'), '/c/d');
      expect(builder.resolve('a', r'c:\b', 'c', 'd'), r'/root/path/a/c:\b/c/d');
      expect(builder.resolve('a', r'\\b', 'c', 'd'), r'/root/path/a/\\b/c/d');
    });
  });

  test('withoutExtension', () {
    expect(builder.withoutExtension(''), '');
    expect(builder.withoutExtension('a'), 'a');
    expect(builder.withoutExtension('.a'), '.a');
    expect(builder.withoutExtension('a.b'), 'a');
    expect(builder.withoutExtension('a/b.c'), 'a/b');
    expect(builder.withoutExtension('a/b.c.d'), 'a/b.c');
    expect(builder.withoutExtension('a/'), 'a/');
    expect(builder.withoutExtension('a/b/'), 'a/b/');
    expect(builder.withoutExtension('a/.'), 'a/.');
    expect(builder.withoutExtension('a/.b'), 'a/.b');
    expect(builder.withoutExtension('a.b/c'), 'a.b/c');
    expect(builder.withoutExtension(r'a.b\c'), r'a');
    expect(builder.withoutExtension(r'a/b\c'), r'a/b\c');
    expect(builder.withoutExtension(r'a/b\c.d'), r'a/b\c');
    expect(builder.withoutExtension('a/b.c/'), 'a/b/');
    expect(builder.withoutExtension('a/b.c//'), 'a/b//');
  });

  test('fromUri', () {
    expect(builder.fromUri(Uri.parse('file:///path/to/foo')), '/path/to/foo');
    expect(builder.fromUri(Uri.parse('file:///path/to/foo/')), '/path/to/foo/');
    expect(builder.fromUri(Uri.parse('file:///')), '/');
    expect(builder.fromUri(Uri.parse('foo/bar')), 'foo/bar');
    expect(builder.fromUri(Uri.parse('/path/to/foo')), '/path/to/foo');
    expect(builder.fromUri(Uri.parse('///path/to/foo')), '/path/to/foo');
    expect(builder.fromUri(Uri.parse('file:///path/to/foo%23bar')),
        '/path/to/foo#bar');
    expect(builder.fromUri(Uri.parse('_%7B_%7D_%60_%5E_%20_%22_%25_')),
        r'_{_}_`_^_ _"_%_');
    expect(() => builder.fromUri(Uri.parse('http://dartlang.org')),
        throwsArgumentError);
  });

  test('toUri', () {
    expect(builder.toUri('/path/to/foo'), Uri.parse('file:///path/to/foo'));
    expect(builder.toUri('/path/to/foo/'), Uri.parse('file:///path/to/foo/'));
    expect(builder.toUri('/'), Uri.parse('file:///'));
    expect(builder.toUri('foo/bar'), Uri.parse('foo/bar'));
    expect(builder.toUri('/path/to/foo#bar'),
        Uri.parse('file:///path/to/foo%23bar'));
    expect(builder.toUri(r'/_{_}_`_^_ _"_%_'),
        Uri.parse('file:///_%7B_%7D_%60_%5E_%20_%22_%25_'));
    expect(builder.toUri(r'_{_}_`_^_ _"_%_'),
        Uri.parse('_%7B_%7D_%60_%5E_%20_%22_%25_'));
  });
}

// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

/**
 * Utilities for encoding and decoding JSON (JavaScript Object Notation) data.
 */

@deprecated
library dart.json;

import "dart:convert";
import "dart:_collection-dev" show deprecated;
import "dart:collection" show HashSet;
export "dart:convert" show JsonUnsupportedObjectError, JsonCyclicError;

// JSON parsing and serialization.

/**
 * *DEPRECATED* Use `dart:convert JSON.decode` instead.
 *
 * Parses [json] and build the corresponding parsed JSON value.
 *
 * Parsed JSON values are of the types [num], [String], [bool], [Null],
 * [List]s of parsed JSON values or [Map]s from [String] to parsed
 * JSON values.
 *
 * The optional [reviver] function, if provided, is called once for each
 * object or list property parsed. The arguments are the property name
 * ([String]) or list index ([int]), and the value is the parsed value.
 * The return value of the reviver will be used as the value of that property
 * instead the parsed value.
 *
 * Throws [FormatException] if the input is not valid JSON text.
 */
@deprecated
parse(String json, [reviver(var key, var value)]) {
  if (reviver != null) {
    var original = reviver;
    reviver = (key, value) => original(key == null ? "" : key, value);
  }
  return JSON.decode(json, reviver: reviver);
}

/**
 * *DEPRECATED* Use `dart:convert JSON.encode` instead.
 *
 * Serializes [object] into a JSON string.
 *
 * Directly serializable values are [num], [String], [bool], and [Null], as well
 * as some [List] and [Map] values.
 * For [List], the elements must all be serializable.
 * For [Map], the keys must be [String] and the values must be serializable.
 *
 * If a value is any other type is attempted serialized, the [toEncodable]
 * method is called with the object as argument, and the result, which must be a
 * directly serializable value, is serialized instead of the original value.
 * If [toEncodable] is omitted, the default is to call `object.toJson()` on the
 * object.
 *
 * If the conversion throws, or returns a value that is not directly
 * serializable, a [JsonUnsupportedObjectError] exception is thrown.
 * If the call throws (including the case where there
 * is no nullary "toJson" method), the error is caught and stored in the
 * [JsonUnsupportedObjectError]'s [:cause:] field.
 *
 * If a [List] or [Map] contains a reference to itself, directly or through
 * other lists or maps, it cannot be serialized and a [JsonCyclicError] is
 * thrown.
 *
 * The objects being serialized should not change during serialization.
 * If an object is serialized more than once, [stringify] is allowed to cache
 * the JSON text for it. I.e., if an object changes after it is first
 * serialized, the new values may or may not be reflected in the result.
 */
@deprecated
String stringify(Object object, [toEncodable(object)]) {
  if (toEncodable == null) toEncodable = _defaultToEncodable;
  return _JsonStringifier.stringify(object, toEncodable);
}

/**
 * *DEPRECATED* Use `package:json/json.dart` or `dart:convert` instead.
 *
 * Serializes [object] into [output] stream.
 *
 * Performs the same operations as [stringify] but outputs the resulting
 * string to an existing [StringSink] instead of creating a new [String].
 *
 * If serialization fails by throwing, some data might have been added to
 * [output], but it won't contain valid JSON text.
 */
@deprecated
void printOn(Object object, StringSink output, [ toEncodable(object) ]) {
  if (toEncodable == null) toEncodable = _defaultToEncodable;
  return _JsonStringifier.printOn(object, output, toEncodable);
}

//// Implementation ///////////////////////////////////////////////////////////

// Simple API for JSON parsing.

/// *DEPRECATED* Use `package:json/json.dart` instead.
@deprecated
abstract class JsonListener {
  void handleString(String value) {}
  void handleNumber(num value) {}
  void handleBool(bool value) {}
  void handleNull() {}
  void beginObject() {}
  void propertyName() {}
  void propertyValue() {}
  void endObject() {}
  void beginArray() {}
  void arrayElement() {}
  void endArray() {}
  /** Called on failure to parse [source]. */
  void fail(String source, int position, String message) {}
}

/**
 * *DEPRECATED* Use `package:json/json.dart` instead.
 *
 * A [JsonListener] that builds data objects from the parser events.
 *
 * This is a simple stack-based object builder. It keeps the most recently
 * seen value in a variable, and uses it depending on the following event.
 */
@deprecated
class BuildJsonListener extends JsonListener {
  /**
   * Stack used to handle nested containers.
   *
   * The current container is pushed on the stack when a new one is
   * started. If the container is a [Map], there is also a current [key]
   * which is also stored on the stack.
   */
  List stack = [];
  /** The current [Map] or [List] being built. */
  var currentContainer;
  /** The most recently read property key. */
  String key;
  /** The most recently read value. */
  var value;

  /** Pushes the currently active container (and key, if a [Map]). */
  void pushContainer() {
    if (currentContainer is Map) stack.add(key);
    stack.add(currentContainer);
  }

  /** Pops the top container from the [stack], including a key if applicable. */
  void popContainer() {
    value = currentContainer;
    currentContainer = stack.removeLast();
    if (currentContainer is Map) key = stack.removeLast();
  }

  void handleString(String value) { this.value = value; }
  void handleNumber(num value) { this.value = value; }
  void handleBool(bool value) { this.value = value; }
  void handleNull() { this.value = null; }

  void beginObject() {
    pushContainer();
    currentContainer = {};
  }

  void propertyName() {
    key = value;
    value = null;
  }

  void propertyValue() {
    Map map = currentContainer;
    map[key] = value;
    key = value = null;
  }

  void endObject() {
    popContainer();
  }

  void beginArray() {
    pushContainer();
    currentContainer = [];
  }

  void arrayElement() {
    List list = currentContainer;
    currentContainer.add(value);
    value = null;
  }

  void endArray() {
    popContainer();
  }

  /** Read out the final result of parsing a JSON string. */
  get result {
    assert(currentContainer == null);
    return value;
  }
}

typedef _Reviver(var key, var value);

/// *DEPRECATED* Use `package:json/json.dart` instead.
@deprecated
class ReviverJsonListener extends BuildJsonListener {
  final _Reviver reviver;
  ReviverJsonListener(reviver(key, value)) : this.reviver = reviver;

  void arrayElement() {
    List list = currentContainer;
    value = reviver(list.length, value);
    super.arrayElement();
  }

  void propertyValue() {
    value = reviver(key, value);
    super.propertyValue();
  }

  get result {
    return reviver("", value);
  }
}

/// *DEPRECATED* Use `package:json/json.dart` instead.
@deprecated
class JsonParser {
  // A simple non-recursive state-based parser for JSON.
  //
  // Literal values accepted in states ARRAY_EMPTY, ARRAY_COMMA, OBJECT_COLON
  // and strings also in OBJECT_EMPTY, OBJECT_COMMA.
  //               VALUE  STRING  :  ,  }  ]        Transitions to
  // EMPTY            X      X                   -> END
  // ARRAY_EMPTY      X      X             @     -> ARRAY_VALUE / pop
  // ARRAY_VALUE                     @     @     -> ARRAY_COMMA / pop
  // ARRAY_COMMA      X      X                   -> ARRAY_VALUE
  // OBJECT_EMPTY            X          @        -> OBJECT_KEY / pop
  // OBJECT_KEY                   @              -> OBJECT_COLON
  // OBJECT_COLON     X      X                   -> OBJECT_VALUE
  // OBJECT_VALUE                    @  @        -> OBJECT_COMMA / pop
  // OBJECT_COMMA            X                   -> OBJECT_KEY
  // END
  // Starting a new array or object will push the current state. The "pop"
  // above means restoring this state and then marking it as an ended value.
  // X means generic handling, @ means special handling for just that
  // state - that is, values are handled generically, only punctuation
  // cares about the current state.
  // Values for states are chosen so bits 0 and 1 tell whether
  // a string/value is allowed, and setting bits 0 through 2 after a value
  // gets to the next state (not empty, doesn't allow a value).

  // State building-block constants.
  static const int INSIDE_ARRAY = 1;
  static const int INSIDE_OBJECT = 2;
  static const int AFTER_COLON = 3;  // Always inside object.

  static const int ALLOW_STRING_MASK = 8;  // Allowed if zero.
  static const int ALLOW_VALUE_MASK = 4;  // Allowed if zero.
  static const int ALLOW_VALUE = 0;
  static const int STRING_ONLY = 4;
  static const int NO_VALUES = 12;

  // Objects and arrays are "empty" until their first property/element.
  static const int EMPTY = 0;
  static const int NON_EMPTY = 16;
  static const int EMPTY_MASK = 16;  // Empty if zero.


  static const int VALUE_READ_BITS = NO_VALUES | NON_EMPTY;

  // Actual states.
  static const int STATE_INITIAL      = EMPTY | ALLOW_VALUE;
  static const int STATE_END          = NON_EMPTY | NO_VALUES;

  static const int STATE_ARRAY_EMPTY  = INSIDE_ARRAY | EMPTY | ALLOW_VALUE;
  static const int STATE_ARRAY_VALUE  = INSIDE_ARRAY | NON_EMPTY | NO_VALUES;
  static const int STATE_ARRAY_COMMA  = INSIDE_ARRAY | NON_EMPTY | ALLOW_VALUE;

  static const int STATE_OBJECT_EMPTY = INSIDE_OBJECT | EMPTY | STRING_ONLY;
  static const int STATE_OBJECT_KEY   = INSIDE_OBJECT | NON_EMPTY | NO_VALUES;
  static const int STATE_OBJECT_COLON = AFTER_COLON | NON_EMPTY | ALLOW_VALUE;
  static const int STATE_OBJECT_VALUE = AFTER_COLON | NON_EMPTY | NO_VALUES;
  static const int STATE_OBJECT_COMMA = INSIDE_OBJECT | NON_EMPTY | STRING_ONLY;

  // Character code constants.
  static const int BACKSPACE       = 0x08;
  static const int TAB             = 0x09;
  static const int NEWLINE         = 0x0a;
  static const int CARRIAGE_RETURN = 0x0d;
  static const int FORM_FEED       = 0x0c;
  static const int SPACE           = 0x20;
  static const int QUOTE           = 0x22;
  static const int PLUS            = 0x2b;
  static const int COMMA           = 0x2c;
  static const int MINUS           = 0x2d;
  static const int DECIMALPOINT    = 0x2e;
  static const int SLASH           = 0x2f;
  static const int CHAR_0          = 0x30;
  static const int CHAR_9          = 0x39;
  static const int COLON           = 0x3a;
  static const int CHAR_E          = 0x45;
  static const int LBRACKET        = 0x5b;
  static const int BACKSLASH       = 0x5c;
  static const int RBRACKET        = 0x5d;
  static const int CHAR_a          = 0x61;
  static const int CHAR_b          = 0x62;
  static const int CHAR_e          = 0x65;
  static const int CHAR_f          = 0x66;
  static const int CHAR_l          = 0x6c;
  static const int CHAR_n          = 0x6e;
  static const int CHAR_r          = 0x72;
  static const int CHAR_s          = 0x73;
  static const int CHAR_t          = 0x74;
  static const int CHAR_u          = 0x75;
  static const int LBRACE          = 0x7b;
  static const int RBRACE          = 0x7d;

  final String source;
  final JsonListener listener;
  JsonParser(this.source, this.listener);

  /** Parses [source], or throws if it fails. */
  void parse() {
    final List<int> states = <int>[];
    int state = STATE_INITIAL;
    int position = 0;
    int length = source.length;
    while (position < length) {
      int char = source.codeUnitAt(position);
      switch (char) {
        case SPACE:
        case CARRIAGE_RETURN:
        case NEWLINE:
        case TAB:
          position++;
          break;
        case QUOTE:
          if ((state & ALLOW_STRING_MASK) != 0) fail(position);
          position = parseString(position + 1);
          state |= VALUE_READ_BITS;
          break;
        case LBRACKET:
          if ((state & ALLOW_VALUE_MASK) != 0) fail(position);
          listener.beginArray();
          states.add(state);
          state = STATE_ARRAY_EMPTY;
          position++;
          break;
        case LBRACE:
          if ((state & ALLOW_VALUE_MASK) != 0) fail(position);
          listener.beginObject();
          states.add(state);
          state = STATE_OBJECT_EMPTY;
          position++;
          break;
        case CHAR_n:
          if ((state & ALLOW_VALUE_MASK) != 0) fail(position);
          position = parseNull(position);
          state |= VALUE_READ_BITS;
          break;
        case CHAR_f:
          if ((state & ALLOW_VALUE_MASK) != 0) fail(position);
          position = parseFalse(position);
          state |= VALUE_READ_BITS;
          break;
        case CHAR_t:
          if ((state & ALLOW_VALUE_MASK) != 0) fail(position);
          position = parseTrue(position);
          state |= VALUE_READ_BITS;
          break;
        case COLON:
          if (state != STATE_OBJECT_KEY) fail(position);
          listener.propertyName();
          state = STATE_OBJECT_COLON;
          position++;
          break;
        case COMMA:
          if (state == STATE_OBJECT_VALUE) {
            listener.propertyValue();
            state = STATE_OBJECT_COMMA;
            position++;
          } else if (state == STATE_ARRAY_VALUE) {
            listener.arrayElement();
            state = STATE_ARRAY_COMMA;
            position++;
          } else {
            fail(position);
          }
          break;
        case RBRACKET:
          if (state == STATE_ARRAY_EMPTY) {
            listener.endArray();
          } else if (state == STATE_ARRAY_VALUE) {
            listener.arrayElement();
            listener.endArray();
          } else {
            fail(position);
          }
          state = states.removeLast() | VALUE_READ_BITS;
          position++;
          break;
        case RBRACE:
          if (state == STATE_OBJECT_EMPTY) {
            listener.endObject();
          } else if (state == STATE_OBJECT_VALUE) {
            listener.propertyValue();
            listener.endObject();
          } else {
            fail(position);
          }
          state = states.removeLast() | VALUE_READ_BITS;
          position++;
          break;
        default:
          if ((state & ALLOW_VALUE_MASK) != 0) fail(position);
          position = parseNumber(char, position);
          state |= VALUE_READ_BITS;
          break;
      }
    }
    if (state != STATE_END) fail(position);
  }

  /**
   * Parses a "true" literal starting at [position].
   *
   * [:source[position]:] must be "t".
   */
  int parseTrue(int position) {
    assert(source.codeUnitAt(position) == CHAR_t);
    if (source.length < position + 4) fail(position, "Unexpected identifier");
    if (source.codeUnitAt(position + 1) != CHAR_r ||
        source.codeUnitAt(position + 2) != CHAR_u ||
        source.codeUnitAt(position + 3) != CHAR_e) {
      fail(position);
    }
    listener.handleBool(true);
    return position + 4;
  }

  /**
   * Parses a "false" literal starting at [position].
   *
   * [:source[position]:] must be "f".
   */
  int parseFalse(int position) {
    assert(source.codeUnitAt(position) == CHAR_f);
    if (source.length < position + 5) fail(position, "Unexpected identifier");
    if (source.codeUnitAt(position + 1) != CHAR_a ||
        source.codeUnitAt(position + 2) != CHAR_l ||
        source.codeUnitAt(position + 3) != CHAR_s ||
        source.codeUnitAt(position + 4) != CHAR_e) {
      fail(position);
    }
    listener.handleBool(false);
    return position + 5;
  }

  /** Parses a "null" literal starting at [position].
   *
   * [:source[position]:] must be "n".
   */
  int parseNull(int position) {
    assert(source.codeUnitAt(position) == CHAR_n);
    if (source.length < position + 4) fail(position, "Unexpected identifier");
    if (source.codeUnitAt(position + 1) != CHAR_u ||
        source.codeUnitAt(position + 2) != CHAR_l ||
        source.codeUnitAt(position + 3) != CHAR_l) {
      fail(position);
    }
    listener.handleNull();
    return position + 4;
  }

  int parseString(int position) {
    // Format: '"'([^\x00-\x1f\\\"]|'\\'[bfnrt/\\"])*'"'
    // Initial position is right after first '"'.
    int start = position;
    int char;
    do {
      if (position == source.length) {
        fail(start - 1, "Unterminated string");
      }
      char = source.codeUnitAt(position);
      if (char == QUOTE) {
        String result = source.substring(start, position);
        listener.handleString(result);
        return position + 1;
      }
      if (char < SPACE) {
        fail(position, "Control character in string");
      }
      position++;
    } while (char != BACKSLASH);
    // Backslash escape detected. Collect character codes for rest of string.
    int firstEscape = position - 1;
    List<int> chars = <int>[];
    while (true) {
      if (position == source.length) {
        fail(start - 1, "Unterminated string");
      }
      char = source.codeUnitAt(position);
      switch (char) {
        case CHAR_b: char = BACKSPACE; break;
        case CHAR_f: char = FORM_FEED; break;
        case CHAR_n: char = NEWLINE; break;
        case CHAR_r: char = CARRIAGE_RETURN; break;
        case CHAR_t: char = TAB; break;
        case SLASH:
        case BACKSLASH:
        case QUOTE:
          break;
        case CHAR_u:
          int hexStart = position - 1;
          int value = 0;
          for (int i = 0; i < 4; i++) {
            position++;
            if (position == source.length) {
              fail(start - 1, "Unterminated string");
            }
            char = source.codeUnitAt(position);
            char -= 0x30;
            if (char < 0) fail(hexStart, "Invalid unicode escape");
            if (char < 10) {
              value = value * 16 + char;
            } else {
              char = (char | 0x20) - 0x31;
              if (char < 0 || char > 5) {
                fail(hexStart, "Invalid unicode escape");
              }
              value = value * 16 + char + 10;
            }
          }
          char = value;
          break;
        default:
          if (char < SPACE) fail(position, "Control character in string");
          fail(position, "Unrecognized string escape");
      }
      do {
        chars.add(char);
        position++;
        if (position == source.length) fail(start - 1, "Unterminated string");
        char = source.codeUnitAt(position);
        if (char == QUOTE) {
          String result = new String.fromCharCodes(chars);
          if (start < firstEscape) {
            result = "${source.substring(start, firstEscape)}$result";
          }
          listener.handleString(result);
          return position + 1;
        }
        if (char < SPACE) {
          fail(position, "Control character in string");
        }
      } while (char != BACKSLASH);
      position++;
    }
  }

  int _handleLiteral(start, position, isDouble) {
    String literal = source.substring(start, position);
    // This correctly creates -0 for doubles.
    num value = (isDouble ? double.parse(literal) : int.parse(literal));
    listener.handleNumber(value);
    return position;
  }

  int parseNumber(int char, int position) {
    // Format:
    //  '-'?('0'|[1-9][0-9]*)('.'[0-9]+)?([eE][+-]?[0-9]+)?
    int start = position;
    int length = source.length;
    bool isDouble = false;
    if (char == MINUS) {
      position++;
      if (position == length) fail(position, "Missing expected digit");
      char = source.codeUnitAt(position);
    }
    if (char < CHAR_0 || char > CHAR_9) {
      fail(position, "Missing expected digit");
    }
    if (char == CHAR_0) {
      position++;
      if (position == length) return _handleLiteral(start, position, false);
      char = source.codeUnitAt(position);
      if (CHAR_0 <= char && char <= CHAR_9) {
        fail(position);
      }
    } else {
      do {
        position++;
        if (position == length) return _handleLiteral(start, position, false);
        char = source.codeUnitAt(position);
      } while (CHAR_0 <= char && char <= CHAR_9);
    }
    if (char == DECIMALPOINT) {
      isDouble = true;
      position++;
      if (position == length) fail(position, "Missing expected digit");
      char = source.codeUnitAt(position);
      if (char < CHAR_0 || char > CHAR_9) fail(position);
      do {
        position++;
        if (position == length) return _handleLiteral(start, position, true);
        char = source.codeUnitAt(position);
      } while (CHAR_0 <= char && char <= CHAR_9);
    }
    if (char == CHAR_e || char == CHAR_E) {
      isDouble = true;
      position++;
      if (position == length) fail(position, "Missing expected digit");
      char = source.codeUnitAt(position);
      if (char == PLUS || char == MINUS) {
        position++;
        if (position == length) fail(position, "Missing expected digit");
        char = source.codeUnitAt(position);
      }
      if (char < CHAR_0 || char > CHAR_9) {
        fail(position, "Missing expected digit");
      }
      do {
        position++;
        if (position == length) return _handleLiteral(start, position, true);
        char = source.codeUnitAt(position);
      } while (CHAR_0 <= char && char <= CHAR_9);
    }
    return _handleLiteral(start, position, isDouble);
  }

  void fail(int position, [String message]) {
    if (message == null) message = "Unexpected character";
    listener.fail(source, position, message);
    // If the listener didn't throw, do it here.
    String slice;
    int sliceEnd = position + 20;
    if (sliceEnd > source.length) {
      slice = "'${source.substring(position)}'";
    } else {
      slice = "'${source.substring(position, sliceEnd)}...'";
    }
    throw new FormatException("Unexpected character at $position: $slice");
  }
}

Object _defaultToEncodable(object) => object.toJson();

class _JsonStringifier {
  final Function toEncodable;
  final StringSink sink;
  final Set<Object> seen;

  _JsonStringifier(this.sink, this.toEncodable)
      : this.seen = new HashSet.identity();

  static String stringify(final object, toEncodable(object)) {
    StringBuffer output = new StringBuffer();
    _JsonStringifier stringifier = new _JsonStringifier(output, toEncodable);
    stringifier.stringifyValue(object);
    return output.toString();
  }

  static void printOn(final object, StringSink output, toEncodable(object)) {
    _JsonStringifier stringifier = new _JsonStringifier(output, toEncodable);
    stringifier.stringifyValue(object);
  }

  static String numberToString(num x) {
    return x.toString();
  }

  // ('0' + x) or ('a' + x - 10)
  static int hexDigit(int x) => x < 10 ? 48 + x : 87 + x;

  static void escape(StringSink sb, String s) {
    final int length = s.length;
    bool needsEscape = false;
    final charCodes = new List<int>();
    for (int i = 0; i < length; i++) {
      int charCode = s.codeUnitAt(i);
      if (charCode < 32) {
        needsEscape = true;
        charCodes.add(JsonParser.BACKSLASH);
        switch (charCode) {
        case JsonParser.BACKSPACE:
          charCodes.add(JsonParser.CHAR_b);
          break;
        case JsonParser.TAB:
          charCodes.add(JsonParser.CHAR_t);
          break;
        case JsonParser.NEWLINE:
          charCodes.add(JsonParser.CHAR_n);
          break;
        case JsonParser.FORM_FEED:
          charCodes.add(JsonParser.CHAR_f);
          break;
        case JsonParser.CARRIAGE_RETURN:
          charCodes.add(JsonParser.CHAR_r);
          break;
        default:
          charCodes.add(JsonParser.CHAR_u);
          charCodes.add(hexDigit((charCode >> 12) & 0xf));
          charCodes.add(hexDigit((charCode >> 8) & 0xf));
          charCodes.add(hexDigit((charCode >> 4) & 0xf));
          charCodes.add(hexDigit(charCode & 0xf));
          break;
        }
      } else if (charCode == JsonParser.QUOTE ||
          charCode == JsonParser.BACKSLASH) {
        needsEscape = true;
        charCodes.add(JsonParser.BACKSLASH);
        charCodes.add(charCode);
      } else {
        charCodes.add(charCode);
      }
    }
    sb.write(needsEscape ? new String.fromCharCodes(charCodes) : s);
  }

  void checkCycle(final object) {
    if (seen.contains(object)) {
      throw new JsonCyclicError(object);
    }
    seen.add(object);
  }

  void stringifyValue(final object) {
    // Tries stringifying object directly. If it's not a simple value, List or
    // Map, call toJson() to get a custom representation and try serializing
    // that.
    if (!stringifyJsonValue(object)) {
      checkCycle(object);
      try {
        var customJson = toEncodable(object);
        if (!stringifyJsonValue(customJson)) {
          throw new JsonUnsupportedObjectError(object);
        }
        seen.remove(object);
      } catch (e) {
        throw new JsonUnsupportedObjectError(object, cause: e);
      }
    }
  }

  /**
   * Serializes a [num], [String], [bool], [Null], [List] or [Map] value.
   *
   * Returns true if the value is one of these types, and false if not.
   * If a value is both a [List] and a [Map], it's serialized as a [List].
   */
  bool stringifyJsonValue(final object) {
    if (object is num) {
      // TODO: use writeOn.
      sink.write(numberToString(object));
      return true;
    } else if (identical(object, true)) {
      sink.write('true');
      return true;
    } else if (identical(object, false)) {
      sink.write('false');
       return true;
    } else if (object == null) {
      sink.write('null');
      return true;
    } else if (object is String) {
      sink.write('"');
      escape(sink, object);
      sink.write('"');
      return true;
    } else if (object is List) {
      checkCycle(object);
      List a = object;
      sink.write('[');
      if (a.length > 0) {
        stringifyValue(a[0]);
        // TODO: switch to Iterables.
        for (int i = 1; i < a.length; i++) {
          sink.write(',');
          stringifyValue(a[i]);
        }
      }
      sink.write(']');
      seen.remove(object);
      return true;
    } else if (object is Map) {
      checkCycle(object);
      Map<String, Object> m = object;
      sink.write('{');
      bool first = true;
      m.forEach((String key, Object value) {
        if (!first) {
          sink.write(',"');
        } else {
          sink.write('"');
        }
        escape(sink, key);
        sink.write('":');
        stringifyValue(value);
        first = false;
      });
      sink.write('}');
      seen.remove(object);
      return true;
    } else {
      return false;
    }
  }
}

/*
 * Copyright (c) 2013, the Dart project authors.
 * 
 * Licensed under the Eclipse Public License v1.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 * 
 * http://www.eclipse.org/legal/epl-v10.html
 * 
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations under
 * the License.
 */
package com.google.dart.engine.context;

/**
 * Instances of the class {@code AnalysisOptions} represent a set of analysis options used to
 * control the behavior of an analysis context.
 */
public class AnalysisOptions {
  /**
   * A flag indicating whether analysis is to use strict mode. In strict mode, error reporting is
   * based exclusively on the static type information.
   */
  private boolean strictMode = false;

  /**
   * Initialize a newly created set of analysis options to have their default values.
   */
  public AnalysisOptions() {
  }

  /**
   * Return {@code true} if analysis is to use strict mode. In strict mode, error reporting is based
   * exclusively on the static type information.
   * 
   * @return {@code true} if analysis is to use strict mode
   */
  public boolean getStrictMode() {
    return strictMode;
  }

  /**
   * Set whether analysis is to use strict mode to the given value. In strict mode, error reporting
   * is based exclusively on the static type information.
   * 
   * @param isStrict {@code true} if analysis is to use strict mode
   */
  public void setStrictMode(boolean isStrict) {
    strictMode = isStrict;
  }
}
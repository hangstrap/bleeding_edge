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
package com.google.dart.engine.internal.resolver;

import com.google.dart.engine.element.ClassElement;
import com.google.dart.engine.element.ExecutableElement;
import com.google.dart.engine.element.LibraryElement;
import com.google.dart.engine.element.MethodElement;
import com.google.dart.engine.element.PropertyAccessorElement;
import com.google.dart.engine.error.AnalysisError;
import com.google.dart.engine.error.ErrorCode;
import com.google.dart.engine.error.StaticTypeWarningCode;
import com.google.dart.engine.error.StaticWarningCode;
import com.google.dart.engine.internal.element.member.MethodMember;
import com.google.dart.engine.internal.element.member.PropertyAccessorMember;
import com.google.dart.engine.internal.verifier.ErrorVerifier;
import com.google.dart.engine.type.FunctionType;
import com.google.dart.engine.type.InterfaceType;
import com.google.dart.engine.type.Type;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.Map.Entry;

/**
 * Instances of the class {@code InheritanceManager} manage the knowledge of where class members
 * (methods, getters & setters) are inherited from.
 * 
 * @coverage dart.engine.resolver
 */
public class InheritanceManager {

  /**
   * The {@link LibraryElement} that is managed by this manager.
   */
  private LibraryElement library;

  /**
   * This is a mapping between each {@link ClassElement} and a map between the {@link String} member
   * names and the associated {@link ExecutableElement} in the mixin and superclass chain.
   */
  private HashMap<ClassElement, MemberMap> classLookup;

  /**
   * This is a mapping between each {@link ClassElement} and a map between the {@link String} member
   * names and the associated {@link ExecutableElement} in the interface set.
   */
  private HashMap<ClassElement, MemberMap> interfaceLookup;

  /**
   * A map between each visited {@link ClassElement} and the set of {@link AnalysisError}s found on
   * the class element.
   */
  private HashMap<ClassElement, HashSet<AnalysisError>> errorsInClassElement = new HashMap<ClassElement, HashSet<AnalysisError>>();

  /**
   * Initialize a newly created inheritance manager.
   * 
   * @param library the library element context that the inheritance mappings are being generated
   */
  public InheritanceManager(LibraryElement library) {
    this.library = library;
    classLookup = new HashMap<ClassElement, MemberMap>();
    interfaceLookup = new HashMap<ClassElement, MemberMap>();
  }

  /**
   * Return the set of {@link AnalysisError}s found on the passed {@link ClassElement}, or
   * {@code null} if there are none.
   * 
   * @param classElt the class element to query
   * @return the set of {@link AnalysisError}s found on the passed {@link ClassElement}, or
   *         {@code null} if there are none
   */
  public HashSet<AnalysisError> getErrors(ClassElement classElt) {
    return errorsInClassElement.get(classElt);
  }

  /**
   * Get and return a mapping between the set of all string names of the members inherited from the
   * passed {@link ClassElement} superclass hierarchy, and the associated {@link ExecutableElement}.
   * 
   * @param classElt the class element to query
   * @return a mapping between the set of all members inherited from the passed {@link ClassElement}
   *         superclass hierarchy, and the associated {@link ExecutableElement}
   */
  public MemberMap getMapOfMembersInheritedFromClasses(ClassElement classElt) {
    return computeClassChainLookupMap(classElt, new HashSet<ClassElement>());
  }

  /**
   * Get and return a mapping between the set of all string names of the members inherited from the
   * passed {@link ClassElement} interface hierarchy, and the associated {@link ExecutableElement}.
   * 
   * @param classElt the class element to query
   * @return a mapping between the set of all string names of the members inherited from the passed
   *         {@link ClassElement} interface hierarchy, and the associated {@link ExecutableElement}.
   */
  public MemberMap getMapOfMembersInheritedFromInterfaces(ClassElement classElt) {
    return computeInterfaceLookupMap(classElt, new HashSet<ClassElement>());
  }

  /**
   * Given some {@link ClassElement class element} and some member name, this returns the
   * {@link ExecutableElement executable element} that the class inherits from the mixins,
   * superclasses or interfaces, that has the member name, if no member is inherited {@code null} is
   * returned.
   * 
   * @param classElt the class element to query
   * @param memberName the name of the executable element to find and return
   * @return the inherited executable element with the member name, or {@code null} if no such
   *         member exists
   */
  public ExecutableElement lookupInheritance(ClassElement classElt, String memberName) {
    if (memberName == null || memberName.isEmpty()) {
      return null;
    }
    ExecutableElement executable = computeClassChainLookupMap(classElt, new HashSet<ClassElement>()).get(
        memberName);
    if (executable == null) {
      return computeInterfaceLookupMap(classElt, new HashSet<ClassElement>()).get(memberName);
    }
    return executable;
  }

  /**
   * Given some {@link ClassElement class element} and some member name, this returns the
   * {@link ExecutableElement executable element} that the class either declares itself, or
   * inherits, that has the member name, if no member is inherited {@code null} is returned.
   * 
   * @param classElt the class element to query
   * @param memberName the name of the executable element to find and return
   * @return the inherited executable element with the member name, or {@code null} if no such
   *         member exists
   */
  public ExecutableElement lookupMember(ClassElement classElt, String memberName) {
    ExecutableElement element = lookupMemberInClass(classElt, memberName);
    if (element != null) {
      return element;
    }
    return lookupInheritance(classElt, memberName);
  }

  /**
   * Given some {@link InterfaceType interface type} and some member name, this returns the
   * {@link FunctionType function type} of the {@link ExecutableElement executable element} that the
   * class either declares itself, or inherits, that has the member name, if no member is inherited
   * {@code null} is returned. The returned {@link FunctionType function type} has all type
   * parameters substituted with corresponding type arguments from the given {@link InterfaceType}.
   * 
   * @param interfaceType the interface type to query
   * @param memberName the name of the executable element to find and return
   * @return the member's function type, or {@code null} if no such member exists
   */
  public FunctionType lookupMemberType(InterfaceType interfaceType, String memberName) {
    ExecutableElement iteratorMember = lookupMember(interfaceType.getElement(), memberName);
    if (iteratorMember == null) {
      return null;
    }
    return substituteTypeArgumentsInMemberFromInheritance(
        iteratorMember.getType(),
        memberName,
        interfaceType);
  }

  /**
   * Set the new library element context.
   * 
   * @param library the new library element
   */
  public void setLibraryElement(LibraryElement library) {
    this.library = library;
  }

  /**
   * This method takes some inherited {@link FunctionType}, and resolves all the parameterized types
   * in the function type, dependent on the class in which it is being overridden.
   * 
   * @param baseFunctionType the function type that is being overridden
   * @param memberName the name of the member, this is used to lookup the inheritance path of the
   *          override
   * @param definingType the type that is overriding the member
   * @return the passed function type with any parameterized types substituted
   */
  public FunctionType substituteTypeArgumentsInMemberFromInheritance(FunctionType baseFunctionType,
      String memberName, InterfaceType definingType) {
    if (baseFunctionType == null) {
      return baseFunctionType;
    }
    // TODO (jwren) add optimization: first check to see if the baseFunctionType has any
    // parameterized types, if it doesn't have any, return the baseFuntionType

    // First, generate the path from the defining type to the overridden member
    LinkedList<InterfaceType> inheritancePath = new LinkedList<InterfaceType>();
    computeInheritancePath(inheritancePath, definingType, memberName);

    if (inheritancePath == null || inheritancePath.isEmpty()) {
      // TODO(jwren) log analysis engine error
      return baseFunctionType;
    }
    FunctionType functionTypeToReturn = baseFunctionType;
    // loop backward through the list substituting as we go:
    while (!inheritancePath.isEmpty()) {
      InterfaceType lastType = inheritancePath.removeLast();
      Type[] parameterTypes = lastType.getElement().getType().getTypeArguments();
      Type[] argumentTypes = lastType.getTypeArguments();
      functionTypeToReturn = functionTypeToReturn.substitute(argumentTypes, parameterTypes);
    }
    return functionTypeToReturn;
  }

  /**
   * Compute and return a mapping between the set of all string names of the members inherited from
   * the passed {@link ClassElement} superclass hierarchy, and the associated
   * {@link ExecutableElement}.
   * 
   * @param classElt the class element to query
   * @param visitedClasses a set of visited classes passed back into this method when it calls
   *          itself recursively
   * @return a mapping between the set of all string names of the members inherited from the passed
   *         {@link ClassElement} superclass hierarchy, and the associated {@link ExecutableElement}
   */
  private MemberMap computeClassChainLookupMap(ClassElement classElt,
      HashSet<ClassElement> visitedClasses) {
    MemberMap resultMap = classLookup.get(classElt);
    if (resultMap != null) {
      return resultMap;
    } else {
      resultMap = new MemberMap();
    }
    ClassElement superclassElt = null;
    InterfaceType supertype = classElt.getSupertype();
    if (supertype != null) {
      superclassElt = supertype.getElement();
    } else {
      // classElt is Object
      classLookup.put(classElt, resultMap);
      return resultMap;
    }
    if (superclassElt != null) {
      if (!visitedClasses.contains(superclassElt)) {
        visitedClasses.add(classElt);
        resultMap = new MemberMap(computeClassChainLookupMap(superclassElt, visitedClasses));
      } else {
        // This case happens only when the superclass was previously visited and not in the lookup,
        // meaning this is meant to shorten the compute for recursive cases.
        classLookup.put(superclassElt, resultMap);
        return resultMap;
      }

      //
      // Substitute the supertypes down the hierarchy
      //
      substituteTypeParametersDownHierarchy(supertype, resultMap);

      //
      // Include the members from the superclass in the resultMap
      //
      recordMapWithClassMembers(resultMap, supertype);
    }

    //
    // Include the members from the mixins in the resultMap
    //
    InterfaceType[] mixins = classElt.getMixins();
    for (int i = mixins.length - 1; i >= 0; i--) {
      recordMapWithClassMembers(resultMap, mixins[i]);
    }

    classLookup.put(classElt, resultMap);
    return resultMap;
  }

  /**
   * Compute and return the inheritance path given the context of a type and a member that is
   * overridden in the inheritance path (for which the type is in the path).
   * 
   * @param chain the inheritance path that is built up as this method calls itself recursively,
   *          when this method is called an empty {@link LinkedList} should be provided
   * @param currentType the current type in the inheritance path
   * @param memberName the name of the member that is being looked up the inheritance path
   */
  private void computeInheritancePath(LinkedList<InterfaceType> chain, InterfaceType currentType,
      String memberName) {
    // TODO (jwren) create a public version of this method which doesn't require the initial chain
    // to be provided, then provided tests for this functionality in InheritanceManagerTest
    chain.add(currentType);
    ClassElement classElt = currentType.getElement();
    InterfaceType supertype = classElt.getSupertype();
    // Base case- reached Object
    if (supertype == null) {
      // Looked up the chain all the way to Object, return null.
      // This should never happen.
      return;
    }

    // If we are done, return the chain
    // We are not done if this is the first recursive call on this method.
    if (chain.size() != 1) {
      // We are done however if the member is in this classElt
      if (lookupMemberInClass(classElt, memberName) != null) {
        return;
      }
    }

    // Otherwise, determine the next type (up the inheritance graph) to search for our member, start
    // with the mixins, followed by the superclass, and finally the interfaces:

    // Mixins- note that mixins call lookupMemberInClass, not lookupMember
    InterfaceType[] mixins = classElt.getMixins();
    for (int i = mixins.length - 1; i >= 0; i--) {
      ClassElement mixinElement = mixins[i].getElement();
      if (mixinElement != null) {
        ExecutableElement elt = lookupMemberInClass(mixinElement, memberName);
        if (elt != null) {
          // this is equivalent (but faster than) calling this method recursively
          // (return computeInheritancePath(chain, mixins[i], memberName);)
          chain.add(mixins[i]);
          return;
        }
      }
    }

    // Superclass
    ClassElement superclassElt = supertype.getElement();
    if (lookupMember(superclassElt, memberName) != null) {
      computeInheritancePath(chain, supertype, memberName);
      return;
    }

    // Interfaces
    InterfaceType[] interfaces = classElt.getInterfaces();
    for (InterfaceType interfaceType : interfaces) {
      ClassElement interfaceElement = interfaceType.getElement();
      if (interfaceElement != null && lookupMember(interfaceElement, memberName) != null) {
        computeInheritancePath(chain, interfaceType, memberName);
        return;
      }
    }
  }

  /**
   * Compute and return a mapping between the set of all string names of the members inherited from
   * the passed {@link ClassElement} interface hierarchy, and the associated
   * {@link ExecutableElement}.
   * 
   * @param classElt the class element to query
   * @param visitedInterfaces a set of visited classes passed back into this method when it calls
   *          itself recursively
   * @return a mapping between the set of all string names of the members inherited from the passed
   *         {@link ClassElement} interface hierarchy, and the associated {@link ExecutableElement}
   */
  private MemberMap computeInterfaceLookupMap(ClassElement classElt,
      HashSet<ClassElement> visitedInterfaces) {
    MemberMap resultMap = interfaceLookup.get(classElt);
    if (resultMap != null) {
      return resultMap;
    } else {
      resultMap = new MemberMap();
    }
    InterfaceType supertype = classElt.getSupertype();
    ClassElement superclassElement = supertype != null ? supertype.getElement() : null;
    InterfaceType[] mixins = classElt.getMixins();
    InterfaceType[] interfaces = classElt.getInterfaces();

    // Recursively collect the list of mappings from all of the interface types
    ArrayList<MemberMap> lookupMaps = new ArrayList<MemberMap>(interfaces.length + mixins.length
        + 1);

    // Superclass element
    if (superclassElement != null) {
      if (!visitedInterfaces.contains(superclassElement)) {
        try {
          visitedInterfaces.add(superclassElement);

          //
          // Recursively compute the map for the supertype.
          //
          MemberMap map = computeInterfaceLookupMap(superclassElement, visitedInterfaces);
          map = new MemberMap(map);

          //
          // Substitute the supertypes down the hierarchy
          //
          substituteTypeParametersDownHierarchy(supertype, map);

          //
          // Add any members from the supertype into the map as well.
          //
          recordMapWithClassMembers(map, supertype);

          lookupMaps.add(map);
        } finally {
          visitedInterfaces.remove(superclassElement);
        }
      } else {
        MemberMap map = interfaceLookup.get(classElt);
        if (map != null) {
          lookupMaps.add(map);
        } else {
          interfaceLookup.put(superclassElement, resultMap);
          return resultMap;
        }
      }
    }

    // Mixin elements
    for (InterfaceType mixinType : mixins) {
      MemberMap mapWithMixinMembers = new MemberMap();
      recordMapWithClassMembers(mapWithMixinMembers, mixinType);
      lookupMaps.add(mapWithMixinMembers);
    }

    // Interface elements
    for (InterfaceType interfaceType : interfaces) {
      ClassElement interfaceElement = interfaceType.getElement();
      if (interfaceElement != null) {
        if (!visitedInterfaces.contains(interfaceElement)) {
          try {
            visitedInterfaces.add(interfaceElement);

            //
            // Recursively compute the map for the interfaces.
            //
            MemberMap map = computeInterfaceLookupMap(interfaceElement, visitedInterfaces);
            map = new MemberMap(map);

            //
            // Substitute the supertypes down the hierarchy
            //
            substituteTypeParametersDownHierarchy(interfaceType, map);

            //
            // And add any members from the interface into the map as well.
            //
            recordMapWithClassMembers(map, interfaceType);

            lookupMaps.add(map);
          } finally {
            visitedInterfaces.remove(interfaceElement);
          }
        } else {
          MemberMap map = interfaceLookup.get(classElt);
          if (map != null) {
            lookupMaps.add(map);
          } else {
            interfaceLookup.put(interfaceElement, resultMap);
            return resultMap;
          }
        }
      }
    }
    if (lookupMaps.size() == 0) {
      interfaceLookup.put(classElt, resultMap);
      return resultMap;
    }

    //
    // Union all of the maps together, grouping the ExecutableElements into sets.
    //
    HashMap<String, HashSet<ExecutableElement>> unionMap = new HashMap<String, HashSet<ExecutableElement>>();
    for (MemberMap lookupMap : lookupMaps) {
      for (int i = 0; i < lookupMap.getSize(); i++) {
        String key = lookupMap.getKey(i);
        if (key == null) {
          break;
        }
        HashSet<ExecutableElement> set = unionMap.get(key);
        if (set == null) {
          set = new HashSet<ExecutableElement>(4);
          unionMap.put(key, set);
        }
        set.add(lookupMap.getValue(i));
      }
    }

    //
    // Loop through the entries in the union map, adding them to the resultMap appropriately.
    //
    for (Entry<String, HashSet<ExecutableElement>> entry : unionMap.entrySet()) {
      String key = entry.getKey();
      HashSet<ExecutableElement> set = entry.getValue();
      int numOfEltsWithMatchingNames = set.size();
      if (numOfEltsWithMatchingNames == 1) {
        resultMap.put(key, set.iterator().next());
      } else {
        boolean allMethods = true;
        boolean allSetters = true;
        boolean allGetters = true;
        for (ExecutableElement executableElement : set) {
          if (executableElement instanceof PropertyAccessorElement) {
            allMethods = false;
            if (((PropertyAccessorElement) executableElement).isSetter()) {
              allGetters = false;
            } else {
              allSetters = false;
            }
          } else {
            allGetters = false;
            allSetters = false;
          }
        }
        if (allMethods || allGetters || allSetters) {
          // Compute the element whose type is the subtype of all of the other types.
          ExecutableElement[] elements = set.toArray(new ExecutableElement[numOfEltsWithMatchingNames]);
          FunctionType[] executableElementTypes = new FunctionType[numOfEltsWithMatchingNames];
          for (int i = 0; i < numOfEltsWithMatchingNames; i++) {
            executableElementTypes[i] = elements[i].getType();
          }
          boolean foundSubtypeOfAllTypes = false;
          for (int i = 0; i < numOfEltsWithMatchingNames; i++) {
            FunctionType subtype = executableElementTypes[i];
            if (subtype == null) {
              continue;
            }
            boolean subtypeOfAllTypes = true;
            for (int j = 0; j < numOfEltsWithMatchingNames && subtypeOfAllTypes; j++) {
              if (i != j) {
                if (!subtype.isSubtypeOf(executableElementTypes[j])) {
                  subtypeOfAllTypes = false;
                  break;
                }
              }
            }
            if (subtypeOfAllTypes) {
              foundSubtypeOfAllTypes = true;
              resultMap.put(key, elements[i]);
              break;
            }
          }
          if (!foundSubtypeOfAllTypes) {
            reportError(
                classElt,
                classElt.getNameOffset(),
                classElt.getDisplayName().length(),
                StaticTypeWarningCode.INCONSISTENT_METHOD_INHERITANCE,
                key);
          }
        } else {
          if (!allMethods && !allGetters) {
            reportError(
                classElt,
                classElt.getNameOffset(),
                classElt.getDisplayName().length(),
                StaticWarningCode.INCONSISTENT_METHOD_INHERITANCE_GETTER_AND_METHOD,
                key);
          }
          resultMap.remove(entry.getKey());
        }
      }
    }
    interfaceLookup.put(classElt, resultMap);
    return resultMap;
  }

  /**
   * Given some {@link ClassElement}, this method finds and returns the {@link ExecutableElement} of
   * the passed name in the class element. Static members, members in super types and members not
   * accessible from the current library are not considered.
   * 
   * @param classElt the class element to query
   * @param memberName the name of the member to lookup in the class
   * @return the found {@link ExecutableElement}, or {@code null} if no such member was found
   */
  private ExecutableElement lookupMemberInClass(ClassElement classElt, String memberName) {
    MethodElement[] methods = classElt.getMethods();
    for (MethodElement method : methods) {
      if (memberName.equals(method.getName()) && method.isAccessibleIn(library)
          && !method.isStatic()) {
        return method;
      }
    }
    PropertyAccessorElement[] accessors = classElt.getAccessors();
    for (PropertyAccessorElement accessor : accessors) {
      if (memberName.equals(accessor.getName()) && accessor.isAccessibleIn(library)
          && !accessor.isStatic()) {
        return accessor;
      }
    }
    return null;
  }

  /**
   * Record the passed map with the set of all members (methods, getters and setters) in the type
   * into the passed map.
   * 
   * @param map some non-{@code null} map to put the methods and accessors from the passed
   *          {@link ClassElement} into
   * @param type the type that will be recorded into the passed map
   */
  private void recordMapWithClassMembers(MemberMap map, InterfaceType type) {
    MethodElement[] methods = type.getMethods();
    for (MethodElement method : methods) {
      if (method.isAccessibleIn(library) && !method.isStatic()) {
        map.put(method.getName(), method);
      }
    }
    PropertyAccessorElement[] accessors = type.getAccessors();
    for (PropertyAccessorElement accessor : accessors) {
      if (accessor.isAccessibleIn(library) && !accessor.isStatic()) {
        map.put(accessor.getName(), accessor);
      }
    }
  }

  /**
   * This method is used to report errors on when they are found computing inheritance information.
   * See {@link ErrorVerifier#checkForInconsistentMethodInheritance()} to see where these generated
   * error codes are reported back into the analysis engine.
   * 
   * @param classElt the location of the source for which the exception occurred
   * @param offset the offset of the location of the error
   * @param length the length of the location of the error
   * @param errorCode the error code to be associated with this error
   * @param arguments the arguments used to build the error message
   */
  private void reportError(ClassElement classElt, int offset, int length, ErrorCode errorCode,
      Object... arguments) {
    HashSet<AnalysisError> errorSet = errorsInClassElement.get(classElt);
    if (errorSet == null) {
      errorSet = new HashSet<AnalysisError>();
      errorsInClassElement.put(classElt, errorSet);
    }
    errorSet.add(new AnalysisError(classElt.getSource(), offset, length, errorCode, arguments));
  }

  /**
   * Loop through all of the members in some {@link MemberMap}, performing type parameter
   * substitutions using a passed supertype.
   * 
   * @param superType the supertype to substitute into the members of the {@link MemberMap}
   * @param map the MemberMap to perform the substitutions on
   */
  private void substituteTypeParametersDownHierarchy(InterfaceType superType, MemberMap map) {
    for (int i = 0; i < map.getSize(); i++) {
      String key = map.getKey(i);
      ExecutableElement executableElement = map.getValue(i);
      if (executableElement instanceof MethodMember) {
        executableElement = MethodMember.from((MethodMember) executableElement, superType);
        map.put(key, executableElement);
      } else if (executableElement instanceof PropertyAccessorMember) {
        executableElement = PropertyAccessorMember.from(
            (PropertyAccessorMember) executableElement,
            superType);
        map.put(key, executableElement);
      }
    }
  }

}

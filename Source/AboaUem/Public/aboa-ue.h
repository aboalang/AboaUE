// Copyright © 2023 - 2025 Christopher Augustus
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// NOTE: !!! std::any fails to cast across shared libraries on macOS
#define ABOA_UE_STD_ANY 1
#if ABOA_UE_STD_ANY
#include <any>
typedef std::any AboaUeDataAny;
#else
typedef void * AboaUeDataAny;
  // ^ TODO: @@@ placeholder for possible rewrite
#endif

#include <map>

struct s7_scheme;

struct AboaUeCode {
  FString const path;
  FString const source;
};

enum struct AboaUeDataType {
  Nothing,
  ActorRef, Bool, Float, MapNameUptr, String,
  UobjectPtr, UobjectRef, Vector, VectorArray
};

struct AboaUeDataRef {
  AboaUeDataAny   const any;
  AboaUeDataType  const type;
};

struct AboaUeDataArg {
  FString         const name;
  AboaUeDataRef   const ref;
};

typedef std::map<FString, AboaUeDataRef>
  AboaUeDataDict;

struct AboaUeState {
  FString const scmPath;
};

struct AboaUeMutant {
  AboaUeState const state;
  s7_scheme * const s7session;
    // ^ we cannot use std::shared_ptr<s7_scheme>
    //              or std::unique_ptr<s7_scheme>
    // because struct s7_scheme is incomplete in s7.h
};

auto bootAboaUe() -> AboaUeMutant;

auto loadAboaUeCode(
  FString const &path) -> AboaUeCode;

auto callAboaUeCode(
  AboaUeMutant    const & mutant,
  FString         const & callee = "",
  AboaUeDataDict  const & args = AboaUeDataDict()
) -> AboaUeDataDict;

auto runAboaUeCode(
  AboaUeMutant    const & mutant,
  AboaUeCode      const & code,
  FString         const & callee = "",
  AboaUeDataDict  const & args = AboaUeDataDict()
) -> AboaUeDataDict;

auto ABOAUEM_API
makeAboaUeDataDict(
  std::initializer_list<AboaUeDataArg> const &
) -> AboaUeDataDict;

auto ABOAUEM_API
makeAboaUeDataActorRef(AActor const &) -> AboaUeDataRef;

auto ABOAUEM_API
makeAboaUeDataFloat(float const &) -> AboaUeDataRef;

auto ABOAUEM_API
makeAboaUeDataMapNameUptr(TMap<FName,TObjectPtr<UObject>> const &) -> AboaUeDataRef;

auto ABOAUEM_API
makeAboaUeDataString(FString const &) -> AboaUeDataRef;

auto ABOAUEM_API
makeAboaUeDataUobjectPtr(UObject const *) -> AboaUeDataRef;

auto ABOAUEM_API
makeAboaUeDataUobjectRef(UObject const &) -> AboaUeDataRef;

auto ABOAUEM_API
makeAboaUeDataVector(FVector const &) -> AboaUeDataRef;

auto ABOAUEM_API
makeAboaUeDataVectorArray(TArray<FVector> const &) -> AboaUeDataRef;

auto ABOAUEM_API
floatFromAboaUeDataDict(
  AboaUeDataDict  const & dict,
  FString         const & key
) -> float;

auto ABOAUEM_API
stringFromAboaUeDataDict(
  AboaUeDataDict  const & dict,
  FString         const & key
) -> FString;

auto ABOAUEM_API
vectorArrayFromAboaUeDataDict(
  AboaUeDataDict  const & dict,
  FString         const & key
) -> TArray<FVector>;

auto ABOAUEM_API
callLoadedAboaUeCode(
  FString         const & callee,
  AboaUeDataDict  const & args = AboaUeDataDict()
) -> AboaUeDataDict;

auto ABOAUEM_API
runCachedAboaUeCodeAtPath(
  FString         const & path,
  FString         const & callee,
  AboaUeDataDict  const & args = AboaUeDataDict(),
  bool                    forceReload = false
) -> AboaUeDataDict;
  // ^ caches and auto-loads observed file changes

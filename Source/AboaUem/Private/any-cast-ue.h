// Copyright © 2024 - 2025 Christopher Augustus
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <any>

auto ueActorPtrFromAny(       std::any const &) -> AActor          const *;
auto ueFloatPtrFromAny(       std::any const &) -> float           const *;
auto ueStringPtrFromAny(      std::any const &) -> FString         const *;
auto ueMapNameUptrFromAny(    std::any const &) -> TMap<FName,TObjectPtr<UObject>> const *;
auto ueObjectPtrFromAny(      std::any const &) -> UObject         const *;
auto ueVectorPtrFromAny(      std::any const &) -> FVector         const *;
auto ueVectorArrayPtrFromAny( std::any const &) -> TArray<FVector> const *;

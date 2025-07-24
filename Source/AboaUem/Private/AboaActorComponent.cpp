// Copyright © 2025 Christopher Augustus
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "AboaActorComponent.h"

#include "EngineMinimal.h"

#include "aboa-ue.h"
#include "aboa-ue-helper.h"

static auto pluginSourcePath(
  FString const & dirPlugin,
  FString const & dirSource,
  FString const & filename
) -> FString {
  return PluginFilePath(dirPlugin, dirSource, filename);
}

UAboaActorComponent::UAboaActorComponent(
  const FObjectInitializer& ObjectInitializer
) : Super(ObjectInitializer)
{
  // contrary with super defaults
  bWantsInitializeComponent                   = true;
  PrimaryComponentTick.bCanEverTick           = true;
  PrimaryComponentTick.bStartWithTickEnabled  = false;
}

// virtual
UAboaActorComponent::~UAboaActorComponent()
{}

// virtual
void UAboaActorComponent::InitializeComponent() {
  Super::InitializeComponent();
  auto results = runCachedAboaUeCodeAtPath(
    pluginSourcePath(AboaDirPlugin, AboaDirSource, AboaFilename),
    AboaNamespace + "-augment-init",
    makeAboaUeDataDict({
      {"uobject", makeAboaUeDataUobjectRef(*this)}}),
    true); // forceReload TODO: ### UNTIL AUTO-RELOAD IS IMPLEMENTED
  //PrintStringToScreen(dumpAboaUeDataDict(results));
    // ^ TODO: ### TRACING
  //PrintStringToScreen(stringFromAboaUeDataDict(results, "result"));
    // ^ TODO: ### TRACING
}

// virtual
void UAboaActorComponent::BeginPlay() {
  Super::BeginPlay();
  auto results = callLoadedAboaUeCode(
    AboaNamespace + "-augment-begin-play",
    makeAboaUeDataDict({
      {"uobject", makeAboaUeDataUobjectRef(*this)}}));
}

// virtual
void UAboaActorComponent::EndPlay(
  const EEndPlayReason::Type EndPlayReason
) {
  Super::EndPlay(EndPlayReason);
  auto results = callLoadedAboaUeCode(
    AboaNamespace + "-augment-end-play",
    makeAboaUeDataDict({
      {"uobject", makeAboaUeDataUobjectRef(*this)}}));
}

// virtual
void UAboaActorComponent::TickComponent(
  float                         DeltaTime,
  enum ELevelTick               TickType,
  FActorComponentTickFunction * ThisTickFunction
) {
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
  auto results = callLoadedAboaUeCode(
    AboaNamespace + "-augment-tick",
    makeAboaUeDataDict({
      {"uobject", makeAboaUeDataUobjectRef(*this)},
      {"delta",   makeAboaUeDataFloat(DeltaTime)}}));
}

// virtual
void UAboaActorComponent::UninitializeComponent() {
  Super::UninitializeComponent();
  auto results = callLoadedAboaUeCode(
    AboaNamespace + "-augment-uninit",
    makeAboaUeDataDict({
      {"uobject", makeAboaUeDataUobjectRef(*this)}}));
}

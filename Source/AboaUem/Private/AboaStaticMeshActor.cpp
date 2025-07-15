// Copyright © 2025 Christopher Augustus
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "AboaStaticMeshActor.h"

#include "AboaActorComponent.h"

AAboaStaticMeshActor::AAboaStaticMeshActor() {
  completeConstruction(0);
}

AAboaStaticMeshActor::AAboaStaticMeshActor(int const inOptions) {
  completeConstruction(inOptions);
}

void AAboaStaticMeshActor::completeConstruction(int const inOptions) {
  AboaActorComponent = CreateDefaultSubobject<UAboaActorComponent>(TEXT("AboaActorComponent"));
}


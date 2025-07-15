// Copyright © 2025 Christopher Augustus
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMeshActor.h"

#include "AboaStaticMeshActor.generated.h"

UCLASS()
class ABOAUEM_API AAboaStaticMeshActor : public AStaticMeshActor {
  GENERATED_BODY()
public:
  AAboaStaticMeshActor();
  AAboaStaticMeshActor(int const inOptions);

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = AboaStaticMeshActor, meta = (AllowPrivateAccess = "true"))
    class UAboaActorComponent* AboaActorComponent;

private:
  void completeConstruction(int const inOptions);
};

// Copyright © 2023 - 2025 Christopher Augustus
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "aboa-ue.h"

#include "aboa-ue-helper.h"
#include "any-cast-ue.h"

#include "aboa-s7.h"

#include "Blueprint/GameViewportSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/ActorComponent.h"
#include "Components/Image.h"
#include "Components/InputComponent.h"
#include "Components/PanelWidget.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/Rotator.h"
#include "Misc/FileHelper.h"

#include <array>
#include <stack>
#include <string>
#include <string_view>
#include <variant>

#define ALK_TRACING 0

DECLARE_LOG_CATEGORY_EXTERN(LogAlkScheme, Log, All);
DEFINE_LOG_CATEGORY(LogAlkScheme);

struct s7pointerError { s7_pointer const pointer; };
struct s7pointerValid { s7_pointer const pointer; };

static auto
scheme_arg_boolean_or_error(
  s7_scheme *  const s7,
  s7_pointer   const arg,
  int          const index,
  char const * const name
) -> std::variant<bool,s7pointerError> {
  if (s7_is_boolean(arg))
    return s7_boolean(s7, arg);
  else
    return s7pointerError({s7_wrong_type_arg_error(
      s7, name, index, arg, "a boolean")});
}

static auto
scheme_arg_c_pointer_or_error(
  s7_scheme *  const s7,
  s7_pointer   const arg,
  int          const index,
  char const * const name
) -> std::variant<s7pointerValid,s7pointerError> {
  if (s7_is_c_pointer(arg))
    return s7pointerValid({arg});
  else
    return s7pointerError({s7_wrong_type_arg_error(
      s7, name, index, arg, "a C pointer")});
}

static auto
scheme_arg_real_or_error(
  s7_scheme *  const s7,
  s7_pointer   const arg,
  int          const index,
  char const * const name
) -> std::variant<s7_double,s7pointerError> {
  if (s7_is_real(arg))
    return s7_real(arg);
  else
    return s7pointerError({s7_wrong_type_arg_error(
      s7, name, index, arg, "a real")});
}

static auto
scheme_arg_float_vector_or_error(
  s7_scheme *  const s7,
  s7_pointer   const arg,
  int          const index,
  char const * const name
) -> std::variant<s7pointerValid,s7pointerError> {
  if (s7_is_float_vector(arg))
    return s7pointerValid({arg});
  else
    return s7pointerError({s7_wrong_type_arg_error(
      s7, name, index, arg, "a float vector")});
}

static auto
scheme_arg_integer_or_error(
  s7_scheme *  const s7,
  s7_pointer   const arg,
  int          const index,
  char const * const name
) -> std::variant<int,s7pointerError> {
  if (s7_is_integer(arg))
    return s7_integer(arg);
  else
    return s7pointerError({s7_wrong_type_arg_error(
      s7, name, index, arg, "an integer")});
}

static auto
scheme_arg_procedure_or_error(
  s7_scheme *  const s7,
  s7_pointer   const arg,
  int          const index,
  char const * const name
) -> std::variant<s7pointerValid,s7pointerError> {
  if (s7_is_procedure(arg))
    return s7pointerValid({arg});
  else
    return s7pointerError({s7_wrong_type_arg_error(
      s7, name, index, arg, "a procedure")});
}

static auto
scheme_arg_string_or_error(
  s7_scheme *  const s7,
  s7_pointer   const arg,
  int          const index,
  char const * const name
) -> std::variant<char const *,s7pointerError> {
  if (s7_is_string(arg))
    return s7_string(arg);
  else
    return s7pointerError({s7_wrong_type_arg_error(
      s7, name, index, arg, "a string")});
}

static auto
scheme_arg_symbol_or_error(
  s7_scheme *  const s7,
  s7_pointer   const arg,
  int          const index,
  char const * const name
) -> std::variant<char const *,s7pointerError> {
  if (s7_is_symbol(arg))
    return s7_symbol_name(arg);
  else
    return s7pointerError({s7_wrong_type_arg_error(
      s7, name, index, arg, "a symbol")});
}

static auto
scheme_arg_symbol_index_or_error(
  s7_scheme *       const s7,
  s7_pointer        const arg,
  int               const index,
  char const *      const name,
  std::string_view  const choices[], // !!! cannot use std::array
  int               const choice_count
) -> std::variant<int,s7pointerError> {
  auto const symbol = scheme_arg_symbol_or_error(s7, arg, index, name);
  if (symbol.index() == 1)
    return std::get<1>(symbol);
  auto const chars = std::get<0>(symbol);
#if 0 // TODO: @@@ MSVC FAILS TO COMPILE THIS VALID CODE
  auto const result = std::find(
    choices, choices + choice_count,
    [&chars](std::string_view const & item) {
      return item.compare(chars) == 0;
    });
#else
  int mutI;
  for (mutI = 0; mutI < choice_count; mutI++)
     if (choices[mutI].compare(chars) == 0)
        break;
  auto const result = choices + mutI;
#endif
  if (result == choices + choice_count)
    return s7pointerError({s7_wrong_type_arg_error(
      s7, name, index, arg, "unmatched symbol")});
  else
    return int(std::distance(choices, result));
}

template <class T>
static auto
scheme_arg_typed_or_error(
  s7_scheme *  const s7,
  s7_pointer   const arg,
  int          const index,
  char const * const name
) -> std::variant<T const *,s7pointerError> {
  auto const argcptr = scheme_arg_c_pointer_or_error(s7, arg, index, name);
  if (argcptr.index() == 1)
    return std::get<1>(argcptr);
  auto const typed = const_cast<T const *>(
    reinterpret_cast<T*>(s7_c_pointer(std::get<0>(argcptr).pointer))); // TODO: ### YIKES!
  if (!typed)
    return s7pointerError({s7_wrong_type_arg_error(
      s7, name, index, arg, "no arg")});
  else
    return typed;
}

template <class T>
static auto
scheme_arg_typed_mut_or_error(
  s7_scheme *  const s7,
  s7_pointer   const arg,
  int          const index,
  char const * const name
) -> std::variant<T *,s7pointerError> {
  auto const argcptr = scheme_arg_c_pointer_or_error(s7, arg, index, name);
  if (argcptr.index() == 1)
    return std::get<1>(argcptr);
  auto const typed = const_cast<T *>(
    reinterpret_cast<T*>(s7_c_pointer(std::get<0>(argcptr).pointer))); // TODO: ### YIKES!
  if (!typed)
    return s7pointerError({s7_wrong_type_arg_error(
      s7, name, index, arg, "no arg")});
  else
    return typed;
}

static auto
s7_hash_table_from_ue_map_name_uptr(
  s7_scheme *                     const   s7,
  TMap<FName,TObjectPtr<UObject>> const & map
) -> s7_pointer {
  auto s7ht = s7_make_hash_table(s7, map.Num());
  for (auto & entry : map) {
    s7_hash_table_set(  s7, s7ht,
      s7_make_string(   s7, TCHAR_TO_ANSI(*entry.Key.ToString())),
      s7_make_c_pointer(s7, entry.Value.Get()));
  }
  return s7ht;
}

static auto
scheme_ue_vector(
  s7_scheme * const s7,
  FVector     const & vec
) -> s7_pointer {
  auto s7vec = s7_make_float_vector(s7, 3, 1, nullptr);
  s7_float_vector_set(s7vec, 0, vec.X);
  s7_float_vector_set(s7vec, 1, vec.Y);
  s7_float_vector_set(s7vec, 2, vec.Z);
  //UE_LOG(LogAlkScheme, Warning, TEXT("scheme_ue_vector %f %f %f"), vec.X, vec.Y, vec.Z);
  return s7vec;
}

static auto
scheme_ue_vector_array(
  s7_scheme *     const s7,
  TArray<FVector> const & uevecarray
) -> s7_pointer {
  auto s7vec = s7_make_vector(s7, uevecarray.Num());
  int i = 0;
  for (auto & uevec : uevecarray)
    s7_vector_set(s7, s7vec, i++, scheme_ue_vector(s7, uevec));
  return s7vec;
}

static auto
ue_vector_from_s7(
  s7_pointer const s7pfvec
) -> FVector {
  auto fve = s7_float_vector_elements(s7pfvec);
  return FVector(fve[0], fve[1], fve[2]);
}

static auto
ue_rotator_from_s7(
  s7_pointer const s7pfvec
) -> FRotator {
  auto fve = s7_float_vector_elements(s7pfvec);
  return FRotator(fve[0], fve[1], fve[2]);
}

static auto
alloc_ue_vector_from_s7(
  s7_pointer const s7pfvec
) -> FVector const & {
  auto fve = s7_float_vector_elements(s7pfvec);
  return *new FVector(fve[0], fve[1], fve[2]);
}

static auto
alloc_ue_vector_array_from_s7(
  s7_scheme * const s7,
  s7_pointer  const s7pvec
) -> TArray<FVector> const & {
  auto arr = new TArray<FVector>(); // TODO: @@@ PRE-ALLOCATE LENGTH
  auto len = s7_vector_length(s7pvec);
  for (int i = 0; i < len; i++)
    arr->Emplace(ue_vector_from_s7(s7_vector_ref(s7, s7pvec, i)));
  return *arr;
}

static auto
call_lambda_with_s7_string(
  s7_scheme *  const s7,
  s7_pointer   const args,
  char const * const name,
  void (* const lambda)(TCHAR const * const)
) -> s7_pointer {
  auto const arg = scheme_arg_string_or_error(s7, s7_car(args), 1, name);
  if (arg.index() == 1)
    return std::get<1>(arg).pointer;
  lambda(ANSI_TO_TCHAR(std::get<0>(arg)));
  return s7_t(s7);
}

static std::array attachrules_symbols {
  std::string_view { "keep-relative" },
  std::string_view { "keep-world" },
  std::string_view { "snap-to-target-excluding-scale" },
  std::string_view { "snap-to-target-including-scale" }
};
static std::array attachrules {
  FAttachmentTransformRules::KeepRelativeTransform,
  FAttachmentTransformRules::KeepWorldTransform,
  FAttachmentTransformRules::SnapToTargetNotIncludingScale,
  FAttachmentTransformRules::SnapToTargetIncludingScale
};

static auto const name_ue_actor_attach_to_actor
                    = "ue-actor-attach-to-actor";
static auto            ue_actor_attach_to_actor(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const argactor = scheme_arg_typed_or_error<AActor>(
    s7, s7_car(args), 1, "actor");
  if (argactor.index() == 1)
    return std::get<1>(argactor).pointer;
  auto const actor = std::get<0>(argactor);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argparent = scheme_arg_typed_or_error<AActor>(
    s7, s7_cadr(args), 2, "parent");
  if (argparent.index() == 1)
    return std::get<1>(argparent).pointer;
  auto const parent = std::get<0>(argparent);
  if (!parent)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argrules = scheme_arg_symbol_index_or_error(
    s7, s7_caddr(args), 3, "rules", attachrules_symbols.data(), attachrules_symbols.size());
  if (argrules.index() == 1)
    return std::get<1>(argrules).pointer;
  auto const rules = attachrules[std::get<0>(argrules)];
  FName socket = NAME_None;
  if (s7_list_length(s7, args) > 3) {
    // !!! NOTE: UE 5.2.1 C++ code will ignore any socket name provided
    auto const argsock = scheme_arg_string_or_error(
      s7, s7_cadddr(args), 4, "socket");
    if (argsock.index() == 1)
        return std::get<1>(argsock).pointer;
    socket = FName(*FString(ANSI_TO_TCHAR(std::get<0>(argsock))));
  }
  return const_cast<AActor*>(actor)->AttachToActor(
    const_cast<AActor*>(parent), rules, socket
  ) ? s7_t(s7) : s7_f(s7);
}

// !!! special API becaush ue-actor-attach-to-actor ignores socket name
static auto const name_ue_actor_attach_to_skeletal_mesh_component_socket
                    = "ue-actor-attach-to-skeletal-mesh-component-socket";
static auto            ue_actor_attach_to_skeletal_mesh_component_socket(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const argactor = scheme_arg_typed_or_error<AActor>(
    s7, s7_car(args), 1, "actor");
  if (argactor.index() == 1)
    return std::get<1>(argactor).pointer;
  auto const actor = std::get<0>(argactor);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argskcomp = scheme_arg_typed_or_error<USkeletalMeshComponent>(
    s7, s7_cadr(args), 2, "component");
  if (argskcomp.index() == 1)
    return std::get<1>(argskcomp).pointer;
  auto const skcomp = std::get<0>(argskcomp);
  if (!skcomp)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const skmesh = skcomp->GetSkeletalMeshAsset();
  if (!skmesh)
    return s7_f(s7);
  auto const argsock = scheme_arg_string_or_error(
    s7, s7_caddr(args), 3, "socket");
  if (argsock.index() == 1)
      return std::get<1>(argsock).pointer;
  auto const socket = FName(*FString(ANSI_TO_TCHAR(std::get<0>(argsock))));
  auto const sksock = skmesh->FindSocket(socket);
  if (!sksock)
    return s7_f(s7);
  return const_cast<USkeletalMeshSocket*>(sksock)->AttachActor(
    const_cast<AActor*>(actor),
    const_cast<USkeletalMeshComponent*>(skcomp)
  ) ? s7_t(s7) : s7_f(s7);
}

static std::array detachrules_symbols {
  std::string_view { "keep-relative" },
  std::string_view { "keep-world" }
};
static std::array detachrules {
  FDetachmentTransformRules::KeepRelativeTransform,
  FDetachmentTransformRules::KeepWorldTransform,
};

static auto const name_ue_actor_detach_from_actor = "ue-actor-detach-from-actor";
static auto
ue_actor_detach_from_actor(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argactor = scheme_arg_typed_or_error<AActor>(
    s7, s7_car(args), 1, "actor");
  if (argactor.index() == 1)
    return std::get<1>(argactor).pointer;
  auto const actor = std::get<0>(argactor);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argrules = scheme_arg_symbol_index_or_error(
    s7, s7_cadr(args), 2, "rules", detachrules_symbols.data(), detachrules_symbols.size());
  if (argrules.index() == 1)
    return std::get<1>(argrules).pointer;
  auto const rules = detachrules[std::get<0>(argrules)];
  const_cast<AActor*>(actor)->DetachFromActor(rules);
  return s7_t(s7);
}

static auto const name_ue_actor_get_location = "ue-actor-get-location";
static auto
ue_actor_get_location(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argactor = scheme_arg_typed_or_error<AActor>(
    s7, s7_car(args), 1, "actor");
  if (argactor.index() == 1)
    return std::get<1>(argactor).pointer;
  auto const actor = std::get<0>(argactor);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  return scheme_ue_vector(s7, actor->GetActorLocation());
}

static auto const name_ue_actor_set_location = "ue-actor-set-location";
static auto
ue_actor_set_location(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argactor = scheme_arg_typed_or_error<AActor>(
    s7, s7_car(args), 1, "actor");
  if (argactor.index() == 1)
    return std::get<1>(argactor).pointer;
  auto const actor = std::get<0>(argactor);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  // TODO: ### FOR NOW ASSUME s7_float_vector RETURNED FROM ue-actor-get-location
  auto const arglocation = scheme_arg_float_vector_or_error(
    s7, s7_cadr(args), 2, "location");
  if (arglocation.index() == 1)
    return std::get<1>(arglocation).pointer;
  auto const location = std::get<0>(arglocation).pointer;
  return const_cast<AActor*>(actor)->SetActorLocation(
    ue_vector_from_s7(location),
    false,    // bool bSweep
    nullptr,  // FHitResult* OutSweepHitResult
    ETeleportType::None
  ) ? s7_t(s7) : s7_f(s7);
}

static auto const name_ue_actor_get_root = "ue-actor-get-root";
static auto
ue_actor_get_root(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argchar = scheme_arg_typed_or_error<ACharacter>(
    s7, s7_car(args), 1, "actor");
  if (argchar.index() == 1)
    return std::get<1>(argchar).pointer;
  auto const actor = std::get<0>(argchar);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  return s7_make_c_pointer(s7, actor->GetRootComponent());
}

static auto const name_ue_actor_get_scale = "ue-actor-get-scale";
static auto
ue_actor_get_scale(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argactor = scheme_arg_typed_or_error<AActor>(
    s7, s7_car(args), 1, "actor");
  if (argactor.index() == 1)
    return std::get<1>(argactor).pointer;
  auto const actor = std::get<0>(argactor);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  return scheme_ue_vector(s7, actor->GetActorScale3D());
}

static auto const name_ue_actor_set_scale = "ue-actor-set-scale";
static auto
ue_actor_set_scale(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argactor = scheme_arg_typed_or_error<AActor>(
    s7, s7_car(args), 1, "actor");
  if (argactor.index() == 1)
    return std::get<1>(argactor).pointer;
  auto const actor = std::get<0>(argactor);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  // TODO: ### FOR NOW ASSUME s7_float_vector RETURNED FROM ue-actor-get-scale
  auto const argscale = scheme_arg_float_vector_or_error(
    s7, s7_cadr(args), 2, "scale");
  if (argscale.index() == 1)
    return std::get<1>(argscale).pointer;
  auto const scale = std::get<0>(argscale).pointer;
  const_cast<AActor*>(actor)->SetActorScale3D(
    ue_vector_from_s7(scale));
  return s7_t(s7);
}

static auto const name_ue_actor_has_tag = "ue-actor-has-tag";
static auto
ue_actor_has_tag(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argactor = scheme_arg_typed_or_error<ACharacter>(
    s7, s7_car(args), 1, "actor");
  if (argactor.index() == 1)
    return std::get<1>(argactor).pointer;
  auto const actor = std::get<0>(argactor);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argtag = scheme_arg_string_or_error(
    s7, s7_cadr(args), 2, "tag");
  if (argtag.index() == 1)
    return std::get<1>(argtag).pointer;
  return actor->Tags.Find(std::get<0>(argtag)) == INDEX_NONE
    ? s7_f(s7) : s7_t(s7);
}

static auto const name_ue_actor_match_tag = "ue-actor-match-tag";
static auto
ue_actor_match_tag(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argactor = scheme_arg_typed_or_error<ACharacter>(
    s7, s7_car(args), 1, "actor");
  if (argactor.index() == 1)
    return std::get<1>(argactor).pointer;
  auto const actor = std::get<0>(argactor);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argtag = scheme_arg_string_or_error(
    s7, s7_cadr(args), 2, "tag");
  if (argtag.index() == 1)
    return std::get<1>(argtag).pointer;
  auto const regex = FRegexPattern(std::get<0>(argtag));
  for (auto const & uefname : actor->Tags) {
    // TODO: @@@ ^ rewrite with FindByPredicate(...)
    auto const uefstring = uefname.ToString();
    if (FRegexMatcher(regex, uefstring).FindNext())
      return s7_make_string(s7, TCHAR_TO_ANSI(*uefstring));
  }
  return s7_f(s7);
}

static auto const name_ue_actor_is_hidden = "ue-actor-is-hidden";
static auto
ue_actor_is_hidden(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argactor = scheme_arg_typed_or_error<AActor>(
    s7, s7_car(args), 1, "actor");
  if (argactor.index() == 1)
    return std::get<1>(argactor).pointer;
  auto const actor = std::get<0>(argactor);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  return s7_make_boolean(s7, actor->IsHidden());
}

static auto const name_ue_actor_set_hidden = "ue-actor-set-hidden";
static auto
ue_actor_set_hidden(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argactor = scheme_arg_typed_or_error<AActor>(
    s7, s7_car(args), 1, "actor");
  if (argactor.index() == 1)
    return std::get<1>(argactor).pointer;
  auto const actor = std::get<0>(argactor);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argtag = scheme_arg_boolean_or_error(
    s7, s7_cadr(args), 2, "hidden");
  if (argtag.index() == 1)
    return std::get<1>(argtag).pointer;
  const_cast<AActor*>(actor)->SetHidden(std::get<0>(argtag));
  return s7_t(s7);
}

static auto const name_ue_actor_is_attached_to
                    = "ue-actor-is-attached-to";
static auto            ue_actor_is_attached_to(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const argactor = scheme_arg_typed_or_error<AActor>(
    s7, s7_car(args), 1, "actor");
  if (argactor.index() == 1)
    return std::get<1>(argactor).pointer;
  auto const actor = std::get<0>(argactor);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argother = scheme_arg_typed_or_error<AActor>(
    s7, s7_cadr(args), 2, "other");
  if (argother.index() == 1)
    return std::get<1>(argother).pointer;
  auto const other = std::get<0>(argother);
  if (!other)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  return actor->IsAttachedTo(std::get<0>(argother))
    ? s7_t(s7) : s7_f(s7);
}

static auto const name_ue_actor_component_get_owner = "ue-actor-component-get-owner";
static auto
ue_actor_component_get_owner(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argcomp = scheme_arg_typed_or_error<UActorComponent>(
    s7, s7_car(args), 1, "component");
  if (argcomp.index() == 1)
    return std::get<1>(argcomp).pointer;
  auto const comp = std::get<0>(argcomp);
  if (!comp)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  return s7_make_c_pointer(s7, comp->GetOwner());
}

static auto const name_ue_character_get_mesh = "ue-character-get-mesh";
static auto
ue_character_get_mesh(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argchar = scheme_arg_typed_or_error<ACharacter>(
    s7, s7_car(args), 1, "character");
  if (argchar.index() == 1)
    return std::get<1>(argchar).pointer;
  auto const character = std::get<0>(argchar);
  if (!character)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  return s7_make_c_pointer(s7, character->GetMesh());
}

class UInputBinding : public UObject {
  DECLARE_CLASS_INTRINSIC(UInputBinding, UObject, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"))
  s7_scheme * s7          = nullptr;
  s7_pointer  mutHandler  = nullptr;
public:
  void BindAction(
    UInputComponent & inputcomp,
    const char *      action,
    EInputEvent const event,
    s7_scheme * const ins7,
    s7_pointer  const proc
  ) {
    if (ins7 && proc && s7_is_procedure(proc)) {
      s7 = ins7;
      mutHandler = proc;
      s7_gc_protect(s7, mutHandler); // TODO: @@@ PROTECTED INDEFINITELY
      inputcomp.BindAction(action, event, this, &UInputBinding::HandleAction);
#if ALK_TRACING
      UE_LOG(LogAlkScheme, Display,
        TEXT("TRACE C++ BindAction %s s7 %d handler %d"),
        event == EInputEvent::IE_Pressed ? TEXT("Pressed") : TEXT("not Pressed"),
        s7, mutHandler);
#endif
    }
  }
  void BindEventHandler(
    UInputComponent & inputcomp,
    EInputEvent const event,
    s7_scheme * const ins7,
    s7_pointer  const proc
  ) {
    if (ins7 && proc && s7_is_procedure(proc)) {
      s7 = ins7;
      mutHandler = proc;
      s7_gc_protect(s7, mutHandler); // TODO: @@@ PROTECTED INDEFINITELY
      inputcomp.BindTouch(event, this, &UInputBinding::HandleEvent);
#if ALK_TRACING
      UE_LOG(LogAlkScheme, Display,
        TEXT("TRACE C++ BindEventHandler %s s7 %d handler %d"),
        event == EInputEvent::IE_Pressed ? TEXT("Pressed") : TEXT("not Pressed"),
        s7, mutHandler);
#endif
    }
  }
  void HandleAction() {
    if (s7 && mutHandler) {
#if ALK_TRACING
    UE_LOG(LogAlkScheme, Display,
      TEXT("TRACE C++ HandleAction s7 %d handler %d"),
      s7, mutHandler);
#endif
      s7_apply_function(s7, mutHandler, s7_nil(s7));
    }
  }
  void HandleEvent(
    ETouchIndex::Type const FingerIndex,
    FVector           const Location
  ) {
    if (s7 && mutHandler) {
#if ALK_TRACING
    UE_LOG(LogAlkScheme, Display,
      TEXT("TRACE C++ HandleEvent s7 %d handler %d"),
      s7, mutHandler);
#endif
      s7_apply_function(s7, mutHandler,
        s7_cons(s7, s7_make_integer(s7, FingerIndex),
          s7_cons(s7, scheme_ue_vector(s7, Location), s7_nil(s7))));
    }
  }
};
IMPLEMENT_CLASS_NO_AUTO_REGISTRATION(UInputBinding)

static std::array input_symbols {
  std::string_view { "pressed" },
  std::string_view { "released" },
  std::string_view { "repeated" }
};
static std::array input_events {
  EInputEvent::IE_Pressed,
  EInputEvent::IE_Released,
  EInputEvent::IE_Repeat
};

static auto const name_ue_bind_input_action = "ue-bind-input-action";
static auto
ue_bind_input_action(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argpawn = scheme_arg_typed_or_error<APawn>(
    s7, s7_car(args), 1, "pawn");
  if (argpawn.index() == 1)
    return std::get<1>(argpawn).pointer;
  auto const pawn = std::get<0>(argpawn);
  if (!pawn)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argaction = scheme_arg_string_or_error(
    s7, s7_cadr(args), 2, "action");
  if (argaction.index() == 1)
    return std::get<1>(argaction).pointer;
  auto const argevent = scheme_arg_symbol_index_or_error(
    s7, s7_caddr(args), 3, "event", input_symbols.data(), input_symbols.size());
  if (argevent.index() == 1)
    return std::get<1>(argevent).pointer;
  auto const inputevent = input_events[std::get<0>(argevent)];
  auto const arghandler = scheme_arg_procedure_or_error(
    s7, s7_cadddr(args), 4, "handler");
  if (arghandler.index() == 1)
    return std::get<1>(arghandler).pointer;
  auto const handler = std::get<0>(arghandler).pointer;
  auto const inputcomp = MutPawnInputComponentOrError(
    *pawn, 0, name_ue_bind_input_action, "");
  if (!inputcomp)
    return s7_f(s7); // TODO: @@@ REPORT ERROR TO SCHEME
  auto const binding = NewObject<UInputBinding>();
  binding->BindAction(
    *inputcomp, std::get<0>(argaction), inputevent, s7, handler);
  // TODO: ### binding LEAKS FROM HERE
  return s7_t(s7);
}

static auto const name_ue_bind_input_touch = "ue-bind-input-touch";
static auto
ue_bind_input_touch(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argworld = scheme_arg_typed_or_error<UWorld>(
    s7, s7_car(args), 1, "world");
  if (argworld.index() == 1)
    return std::get<1>(argworld).pointer;
  auto const world = std::get<0>(argworld);
  if (!world)
    return s7_f(s7); // !!! scheme_arg already checks for null
  auto const argevent = scheme_arg_symbol_index_or_error(
    s7, s7_cadr(args), 2, "event", input_symbols.data(), input_symbols.size());
  if (argevent.index() == 1)
    return std::get<1>(argevent).pointer;
  auto const inputevent = input_events[std::get<0>(argevent)];
  auto const arghandler = scheme_arg_procedure_or_error(
    s7, s7_caddr(args), 3, "handler");
  if (arghandler.index() == 1)
    return std::get<1>(arghandler).pointer;
  auto const handler = std::get<0>(arghandler).pointer;
  auto const inputcomp = MutPlayerInputComponentOrError(
    *world, 0, name_ue_bind_input_touch, "");
  if (!inputcomp)
    return s7_f(s7); // TODO: @@@ REPORT ERROR TO SCHEME
  auto const binding = NewObject<UInputBinding>();
  binding->BindEventHandler(*inputcomp, inputevent, s7, handler);
  // TODO: ### binding LEAKS FROM HERE
  return s7_t(s7);
}

static auto const name_ue_find_uclass_by_name
                    = "ue-find-uclass-by-name";
static auto            ue_find_uclass_by_name(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const argname = scheme_arg_string_or_error(
    s7, s7_car(args), 1, "name");
  if (argname.index() == 1)
    return std::get<1>(argname).pointer;
  return s7_make_c_pointer(s7,
    FindObject<UClass>(ANY_PACKAGE,
      ANSI_TO_TCHAR(std::get<0>(argname))));
}

#if 0
static auto
ue_apply_procedure_on_world(
  s7_scheme * const s7,
  s7_pointer  const proc,
  UWorld & mutWorld
) -> void {
#if ALK_TRACING
  UE_LOG(LogAlkScheme, Display,
    TEXT("TRACE C++ ue_apply_procedure_on_world \"%s\""),
    *mutWorld.OriginalWorldName.ToString());
#endif
  s7_apply_function(s7, proc,
    s7_cons(s7, s7_make_c_pointer(s7, &mutWorld), s7_nil(s7)));
}

static auto const name_ue_hook_on_world_added = "ue-hook-on-world-added";
static auto
ue_hook_on_world_added(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const arghandler = scheme_arg_procedure_or_error(
    s7, s7_car(args), 1, "handler");
  if (arghandler.index() == 1)
    return std::get<1>(arghandler).pointer;
  auto const handler = std::get<0>(arghandler).pointer;
  auto * mutEngine = MutEngineOrError(name_ue_hook_on_world_added, "");
  if (!mutEngine)
    return s7_f(s7); // TODO: @@@ REPORT ERROR TO SCHEME
  s7_gc_protect(s7, handler); // TODO: @@@ PROTECTED INDEFINITELY
#if 0 // !!! OnWorldAdded() never gets called except in one case which is useless
  mutEngine->OnWorldAdded().AddLambda(
    [s7, handler](UWorld * mutWorld) {
      if (mutWorld)
        ue_apply_procedure_on_world(s7, handler, *mutWorld);
    });
#endif
  auto const lambda = [s7, handler]() {
    ApplyLambdaOnAllWorlds(
      [s7, handler](UWorld & mutWorld) {
        ue_apply_procedure_on_world(s7, handler, mutWorld);
      });
  };
  if (GUnrealEd) // !!! only way to be notified of new worlds
    GUnrealEd->OnViewportClientListChanged().AddLambda(lambda);
  lambda();
  return s7_t(s7);
}
#endif

static auto const name_ue_hook_on_game_viewport_subsystem_widget_added
                    = "ue-hook-on-game-viewport-subsystem-widget-added";
static auto            ue_hook_on_game_viewport_subsystem_widget_added(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const arghandler = scheme_arg_procedure_or_error(
    s7, s7_car(args), 1, "handler");
  if (arghandler.index() == 1)
    return std::get<1>(arghandler).pointer;
  auto const handler = std::get<0>(arghandler).pointer;
  s7_gc_protect(s7, handler); // TODO: @@@ PROTECTED INDEFINITELY
  auto const ugvs = UGameViewportSubsystem::Get();
  if (!ugvs)
    return s7_f(s7);
  ugvs->OnWidgetAdded.AddLambda(
    [s7, handler](UWidget* uwidget, ULocalPlayer* ulocalplayer) {
      s7_apply_function(s7, handler,
        s7_cons(s7, s7_make_c_pointer(s7, uwidget), s7_nil(s7)));
    });
  return s7_t(s7);
}

static auto const name_ue_hook_on_world_begin_play = "ue-hook-on-world-begin-play";
static auto
ue_hook_on_world_begin_play(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const arghandler = scheme_arg_procedure_or_error(
    s7, s7_car(args), 1, "handler");
  if (arghandler.index() == 1)
    return std::get<1>(arghandler).pointer;
  auto const handler = std::get<0>(arghandler).pointer;
  s7_gc_protect(s7, handler); // TODO: @@@ PROTECTED INDEFINITELY
#if ALK_TRACING
  UE_LOG(LogAlkScheme, Display, TEXT("TRACE C++ %s"),
    ANSI_TO_TCHAR(name_ue_hook_on_world_begin_play));
#endif
  FWorldDelegates::OnWorldInitializedActors.AddLambda(
    [s7, handler](const UWorld::FActorsInitializedParams & params) {
#if ALK_TRACING
      UE_LOG(LogAlkScheme, Display, TEXT("TRACE C++ on world actors initialized"));
#endif
      auto const world = params.World;
      if (world)
        world->OnWorldBeginPlay.AddLambda([s7, handler, world]() {
          s7_apply_function(s7, handler,
            s7_cons(s7, s7_make_c_pointer(s7, world), s7_nil(s7)));
        });
    });
  ApplyLambdaOnAllWorlds([s7, handler](UWorld & mutWorld) {
    if (mutWorld.HasBegunPlay())
      s7_apply_function(s7, handler,
        s7_cons(s7, s7_make_c_pointer(s7, &mutWorld), s7_nil(s7)));
  });
  return s7_t(s7);
}

static auto const name_ue_log = "ue-log";
static auto
ue_log(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  return call_lambda_with_s7_string(s7, args, name_ue_log,
    [](TCHAR const * const text) {
      UE_LOG(LogAlkScheme, Display, TEXT("%s"), text);
    });
}

static auto const name_ue_material_instance_dynamic_set_scalar_parameter_value
  = "ue-material-instance-dynamic-set-scalar-parameter-value";
static auto
ue_material_instance_dynamic_set_scalar_parameter_value(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const arginst = scheme_arg_typed_mut_or_error<UMaterialInstanceDynamic>(
    s7, s7_car(args), 1, "instance");
  if (arginst.index() == 1)
    return std::get<1>(arginst).pointer;
  auto const argname = scheme_arg_string_or_error(
    s7, s7_cadr(args), 2, "name");
  if (argname.index() == 1)
    return std::get<1>(argname).pointer;
  auto const argvalue = scheme_arg_real_or_error(
    s7, s7_caddr(args), 3, "value");
  if (argvalue.index() == 1)
    return std::get<1>(argvalue).pointer;
  auto const instance = std::get<0>(arginst);
  if (!instance)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  instance->SetScalarParameterValue(
    std::get<0>(argname),
    std::get<0>(argvalue));
  return s7_t(s7);
}

static auto const name_umg_image_set_brush_from_texture
                    = "umg-image-set-brush-from-texture";
static auto            umg_image_set_brush_from_texture(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const argimage = scheme_arg_typed_mut_or_error<UImage>(
    s7, s7_car(args), 1, "image");
  if (argimage.index() == 1)
    return std::get<1>(argimage).pointer;
  auto const image = std::get<0>(argimage);
  if (!image)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argtexture = scheme_arg_typed_mut_or_error<UTexture2D>(
    s7, s7_cadr(args), 2, "texture");
  if (argtexture.index() == 1)
    return std::get<1>(argtexture).pointer;
  auto const texture = std::get<0>(argtexture);
  if (!texture)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argmatch = scheme_arg_boolean_or_error(
    s7, s7_caddr(args), 3, "match");
  if (argmatch.index() == 1)
    return std::get<1>(argmatch).pointer;
  image->SetBrushFromTexture(texture, std::get<0>(argmatch));
  return s7_t(s7);
}

static auto const name_umg_panel_widget_get_child_at
                    = "umg-panel-widget-get-child-at";
static auto            umg_panel_widget_get_child_at(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const argpanel = scheme_arg_typed_or_error<UPanelWidget>(
    s7, s7_car(args), 1, "panel");
  if (argpanel.index() == 1)
    return std::get<1>(argpanel).pointer;
  auto const panel = std::get<0>(argpanel);
  if (!panel || !panel->CanHaveMultipleChildren())
    return s7_f(s7); // ### TODO: INDICATE ERROR
  auto const argindex = scheme_arg_integer_or_error(
    s7, s7_cadr(args), 2, "index");
  if (argindex.index() == 1)
    return std::get<1>(argindex).pointer;
  auto const index = std::get<0>(argindex);
  if (index >= panel->GetChildrenCount())
    return s7_f(s7); // ### TODO: INDICATE ERROR
  return s7_make_c_pointer(s7, panel->GetChildAt(index));
}

static auto const name_ue_primitive_component_add_force
                    = "ue-primitive-component-add-force";
static auto            ue_primitive_component_add_force(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const argcomp = scheme_arg_typed_or_error<UPrimitiveComponent>(
    s7, s7_car(args), 1, "component");
  if (argcomp.index() == 1) // !!! scheme_arg_typed_or_error already checks for null
    return std::get<1>(argcomp).pointer;
  auto const component = std::get<0>(argcomp);
  auto const argimp = scheme_arg_float_vector_or_error(
    s7, s7_cadr(args), 2, "force");
  if (argimp.index() == 1)
    return std::get<1>(argimp).pointer;
  auto const force = std::get<0>(argimp).pointer;
  const_cast<UPrimitiveComponent*>(component)->AddForce(
    ue_vector_from_s7(force));
  return s7_t(s7);
}

static auto const name_ue_primitive_component_add_impulse
                    = "ue-primitive-component-add-impulse";
static auto            ue_primitive_component_add_impulse(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const argcomp = scheme_arg_typed_or_error<UPrimitiveComponent>(
    s7, s7_car(args), 1, "component");
  if (argcomp.index() == 1) // !!! scheme_arg_typed_or_error already checks for null
    return std::get<1>(argcomp).pointer;
  auto const component = std::get<0>(argcomp);
  auto const argimp = scheme_arg_float_vector_or_error(
    s7, s7_cadr(args), 2, "impulse");
  if (argimp.index() == 1)
    return std::get<1>(argimp).pointer;
  auto const impulse = std::get<0>(argimp).pointer;
  const_cast<UPrimitiveComponent*>(component)->AddImpulse(
    ue_vector_from_s7(impulse));
  return s7_t(s7);
}

static auto const name_ue_primitive_component_get_material
  = "ue-primitive-component-get-material";
static auto
ue_primitive_component_get_material(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const argcomp = scheme_arg_typed_or_error<UPrimitiveComponent>(
    s7, s7_car(args), 1, "component");
  if (argcomp.index() == 1)
    return std::get<1>(argcomp).pointer;
  auto const argindex = scheme_arg_integer_or_error(
    s7, s7_cadr(args), 2, "index");
  if (argindex.index() == 1)
    return std::get<1>(argindex).pointer;
  auto const component = std::get<0>(argcomp);
  return component
    ? s7_make_c_pointer(s7, component->GetMaterial(std::get<0>(argindex)))
    : s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
}

static auto const name_ue_primitive_component_set_material
  = "ue-primitive-component-set-material";
static auto
ue_primitive_component_set_material(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const argcomp = scheme_arg_typed_or_error<UPrimitiveComponent>(
    s7, s7_car(args), 1, "component");
  if (argcomp.index() == 1)
    return std::get<1>(argcomp).pointer;
  auto const component = std::get<0>(argcomp);
  if (!component)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argindex = scheme_arg_integer_or_error(
    s7, s7_cadr(args), 2, "index");
  if (argindex.index() == 1)
    return std::get<1>(argindex).pointer;
  auto const argmat = scheme_arg_typed_or_error<UMaterialInterface>(
    s7, s7_caddr(args), 3, "material");
  if (argmat.index() == 1)
    return std::get<1>(argmat).pointer;
  auto const material = std::get<0>(argmat);
  const_cast<UPrimitiveComponent*>(component)->SetMaterial(
    std::get<0>(argindex), const_cast<UMaterialInterface*>(material));
  return s7_t(s7);
}

static auto const name_ue_primitive_component_set_simulate_physics
                    = "ue-primitive-component-set-simulate-physics";
static auto            ue_primitive_component_set_simulate_physics(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const argcomp = scheme_arg_typed_or_error<UPrimitiveComponent>(
    s7, s7_car(args), 1, "component");
  if (argcomp.index() == 1)
    return std::get<1>(argcomp).pointer;
  auto const component = std::get<0>(argcomp);
  if (!component)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argsim = scheme_arg_boolean_or_error(
    s7, s7_cadr(args), 2, "simulate");
  if (argsim.index() == 1)
    return std::get<1>(argsim).pointer;
  const_cast<UPrimitiveComponent*>(component)->SetSimulatePhysics(
    std::get<0>(argsim));
  return s7_t(s7);
}

static auto const name_ue_print_string = "ue-print-string";
static auto
ue_print_string(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argworld = scheme_arg_typed_or_error<UWorld>(
    s7, s7_car(args), 1, "world");
  if (argworld.index() == 1)
    return std::get<1>(argworld).pointer;
  auto const world = std::get<0>(argworld);
  auto const argstring = scheme_arg_string_or_error(
    s7, s7_cadr(args), 2, "string");
  if (argstring.index() == 1)
    return std::get<1>(argstring).pointer;
  PrintStringToScreen(
    FString(ANSI_TO_TCHAR(std::get<0>(argstring))),
    world);
  return s7_t(s7);
}

static auto const name_ue_print_string_primary = "ue-print-string-primary";
static auto
ue_print_string_primary(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argstring = scheme_arg_string_or_error(
    s7, s7_car(args), 1, "string");
  if (argstring.index() == 1)
    return std::get<1>(argstring).pointer;
  PrintStringToScreen(
    FString(ANSI_TO_TCHAR(std::get<0>(argstring))));
  return s7_t(s7);
}

static auto const name_ue_scene_component_find_skeletal_mesh
  = "ue-scene-component-find-skeletal-mesh";
static auto
ue_scene_component_find_skeletal_mesh(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argchar = scheme_arg_typed_or_error<USceneComponent>(
    s7, s7_car(args), 1, "component");
  if (argchar.index() == 1)
    return std::get<1>(argchar).pointer;
  auto const component = std::get<0>(argchar);
  if (!component)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  TArray<USceneComponent*> children;
  component->GetChildrenComponents(true /* bIncludeAllDescendents */, children);
  for (auto const child : children) {
    auto const skelmesh = dynamic_cast<USkeletalMeshComponent*>(child);
    if (skelmesh)
      return s7_make_c_pointer(s7, skelmesh);
  }
  return s7_nil(s7);
}

static auto const name_ue_scene_component_set_visibility
  = "ue-scene-component-set-visibility";
static auto
ue_scene_component_set_visibility(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const argchar = scheme_arg_typed_or_error<USceneComponent>(
    s7, s7_car(args), 1, "component");
  if (argchar.index() == 1)
    return std::get<1>(argchar).pointer;
  auto const component = std::get<0>(argchar);
  if (!component)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argtag = scheme_arg_boolean_or_error(
    s7, s7_cadr(args), 2, "visible");
  if (argtag.index() == 1)
    return std::get<1>(argtag).pointer;
  const_cast<USceneComponent*>(component)->SetVisibility(std::get<0>(argtag));
  return s7_t(s7);
}

static auto const name_ue_uobject_get_class_name
                    = "ue-uobject-get-class-name";
static auto            ue_uobject_get_class_name(
  s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const arguobj = scheme_arg_typed_or_error<UObject>(
    s7, s7_car(args), 1, "uobject");
  if (arguobj.index() == 1)
    return std::get<1>(arguobj).pointer;
  auto const uobject = std::get<0>(arguobj);
  return uobject
    ? s7_make_string(s7, TCHAR_TO_ANSI(*uobject->GetClass()->GetName()))
    : s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
}

static auto const name_ue_uobject_get_display_name
                    = "ue-uobject-get-display-name";
static auto            ue_uobject_get_display_name(
  s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const arguobject = scheme_arg_typed_or_error<AActor>(
    s7, s7_car(args), 1, "uobject");
  if (arguobject.index() == 1)
    return std::get<1>(arguobject).pointer;
  auto const uobject = std::get<0>(arguobject);
  if (!uobject)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  return s7_make_string(s7, TCHAR_TO_ANSI(
    *UKismetSystemLibrary::GetDisplayName(uobject)));
}

static auto const name_umg_user_widget_get_root_widget
                    = "umg-user-widget-get-root-widget";
static auto            umg_user_widget_get_root_widget(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const argwid = scheme_arg_typed_or_error<UUserWidget>(
    s7, s7_car(args), 1, "widget");
  if (argwid.index() == 1)
    return std::get<1>(argwid).pointer;
  auto const widget = std::get<0>(argwid);
  return widget
    ? s7_make_c_pointer(s7, widget->GetRootWidget())
    : s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
}

static auto const name_ue_vector_rotate_euler
                    = "ue-vector-rotate-euler";
static auto            ue_vector_rotate_euler(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const argvec = scheme_arg_float_vector_or_error(
    s7, s7_car(args), 1, "vector");
  if (argvec.index() == 1)
    return std::get<1>(argvec).pointer;
  auto const vector = std::get<0>(argvec).pointer;
  auto const argang = scheme_arg_float_vector_or_error(
    s7, s7_cadr(args), 2, "angles");
  if (argang.index() == 1)
    return std::get<1>(argang).pointer;
  auto const angles = std::get<0>(argang).pointer;
  return scheme_ue_vector(s7,
    FRotator3d::MakeFromEuler(
      ue_vector_from_s7(angles)).RotateVector(
      ue_vector_from_s7(vector)));
}

static auto const name_ue_world_current_destroy_actor
                    = "ue-world-current-destroy-actor";
static auto
ue_world_current_destroy_actor(s7_scheme * s7, s7_pointer args) -> s7_pointer {
  auto const world = CurrentPlayWorld();
  if (!world)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argactor = scheme_arg_typed_or_error<AActor>(
    s7, s7_car(args), 1, "actor");
  if (argactor.index() == 1)
    return std::get<1>(argactor).pointer;
  auto const actor = std::get<0>(argactor);
  if (!actor)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  return const_cast<UWorld*>(world)->DestroyActor(
      const_cast<AActor*>(actor))
    ? s7_t(s7) : s7_f(s7);
}

static auto const name_ue_world_current_get_game_viewport
                    = "ue-world-current-get-game-viewport";
static auto            ue_world_current_get_game_viewport(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const world = CurrentPlayWorld();
  if (!world)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  return s7_make_c_pointer(s7, world->GetGameViewport());
}

static auto const name_ue_world_current_spawn_actor
                    = "ue-world-current-spawn-actor";
static auto            ue_world_current_spawn_actor(
  s7_scheme * s7, s7_pointer args
) -> s7_pointer {
  auto const world = CurrentPlayWorld();
  if (!world)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argclass = scheme_arg_typed_or_error<UClass>(
    s7, s7_car(args), 1, "class");
  if (argclass.index() == 1)
    return std::get<1>(argclass).pointer;
  auto const uclass = std::get<0>(argclass);
  if (!uclass)
    return s7_f(s7); // !!! scheme_arg_typed_or_error already checks for null
  auto const argloc = scheme_arg_float_vector_or_error(
    s7, s7_cadr(args), 2, "location");
  if (argloc.index() == 1)
    return std::get<1>(argloc).pointer;
  auto const argrot = scheme_arg_float_vector_or_error(
    s7, s7_caddr(args), 3, "rotation");
  if (argrot.index() == 1)
    return std::get<1>(argrot).pointer;
  auto const location = ue_vector_from_s7( std::get<0>(argloc).pointer);
  auto const rotation = ue_rotator_from_s7(std::get<0>(argrot).pointer);
  return s7_make_c_pointer(s7,
    const_cast<UWorld*>(world)->SpawnActor(
      const_cast<UClass*>(uclass), &location, &rotation));
}

static auto function_help_string(
  char const * const name,
  char const * const args
) -> std::string {
  return std::string("(") + name + args + ")";
}

auto bootAboaUe() -> AboaUeMutant {
  auto s7session = s7_init();
  if (!s7session) {
    UE_LOG(LogAlkScheme, Error, TEXT("Failed to init s7 Scheme"))
    return {};
  }
  s7_define_function(s7session,
    name_ue_actor_attach_to_actor,
         ue_actor_attach_to_actor,
    3, 1, false, function_help_string(
    name_ue_actor_attach_to_actor,
      " actor parent rules socket").c_str());
  s7_define_function(s7session,
    name_ue_actor_attach_to_skeletal_mesh_component_socket,
         ue_actor_attach_to_skeletal_mesh_component_socket,
    3, 0, false, function_help_string(
    name_ue_actor_attach_to_skeletal_mesh_component_socket,
      " actor component rules socket").c_str());
  s7_define_function(s7session,
    name_ue_actor_detach_from_actor,
         ue_actor_detach_from_actor,
    2, 0, false, function_help_string(
    name_ue_actor_detach_from_actor,
      " actor rules").c_str());
  s7_define_function(s7session,
    name_ue_actor_get_location, ue_actor_get_location, 1, 0, false,
    function_help_string(  name_ue_actor_get_location,  " actor").c_str());
  s7_define_function(s7session,
    name_ue_actor_set_location, ue_actor_set_location, 2, 0, false,
    function_help_string(  name_ue_actor_set_location, " actor location").c_str());
  s7_define_function(s7session,
    name_ue_actor_get_root,     ue_actor_get_root, 1, 0, false,
    function_help_string(  name_ue_actor_get_root, " actor").c_str());
  s7_define_function(s7session,
    name_ue_actor_get_scale,
         ue_actor_get_scale,
    1, 0, false, function_help_string(
    name_ue_actor_get_scale,
      " actor").c_str());
  s7_define_function(s7session,
    name_ue_actor_set_scale,
         ue_actor_set_scale,
    2, 0, false, function_help_string(
    name_ue_actor_set_scale,
      " actor scale").c_str());
  s7_define_function(s7session,
    name_ue_actor_has_tag,
         ue_actor_has_tag,
    2, 0, false, function_help_string(
    name_ue_actor_has_tag,
      " actor tag").c_str());
  s7_define_function(s7session,
    name_ue_actor_match_tag,
         ue_actor_match_tag,
    2, 0, false, function_help_string(
    name_ue_actor_match_tag,
      " actor tag").c_str());
  s7_define_function(s7session,
    name_ue_actor_is_hidden,
         ue_actor_is_hidden,
    1, 0, false, function_help_string(
    name_ue_actor_is_hidden,
      " actor").c_str());
  s7_define_function(s7session,
    name_ue_actor_set_hidden,
         ue_actor_set_hidden,
    2, 0, false, function_help_string(
    name_ue_actor_set_hidden,
      " actor hidden").c_str());
  s7_define_function(s7session,
    name_ue_actor_is_attached_to,
         ue_actor_is_attached_to,
    2, 0, false, function_help_string(
    name_ue_actor_is_attached_to,
      " actor other").c_str());
  s7_define_function(s7session,
    name_ue_actor_component_get_owner,
         ue_actor_component_get_owner,
    1, 0, false, function_help_string(
    name_ue_actor_component_get_owner,
      " component").c_str());
  s7_define_function(s7session,
    name_ue_character_get_mesh,
         ue_character_get_mesh,
    1, 0, false, function_help_string(
    name_ue_character_get_mesh,
      " character").c_str());
  s7_define_function(s7session,
    name_ue_bind_input_action, ue_bind_input_action, 4, 0, false,
    function_help_string( name_ue_bind_input_action, " pawn action input handler").c_str());
  s7_define_function(s7session,
    name_ue_bind_input_touch, ue_bind_input_touch, 3, 0, false,
    function_help_string(name_ue_bind_input_touch, " world event handler").c_str());
  s7_define_function(s7session,
    name_ue_find_uclass_by_name,
         ue_find_uclass_by_name,
    1, 0, false, function_help_string(
    name_ue_find_uclass_by_name,
      " name").c_str());
  s7_define_function(s7session,
    name_umg_user_widget_get_root_widget,
         umg_user_widget_get_root_widget,
    1, 0, false, function_help_string(
    name_umg_user_widget_get_root_widget,
      " widget").c_str());
  s7_define_function(s7session,
    name_ue_hook_on_game_viewport_subsystem_widget_added,
         ue_hook_on_game_viewport_subsystem_widget_added,
    1, 0, false,
    function_help_string(
    name_ue_hook_on_game_viewport_subsystem_widget_added,
      " handler").c_str());
  s7_define_function(s7session,
    name_ue_hook_on_world_begin_play, ue_hook_on_world_begin_play, 1, 0, false,
    function_help_string(name_ue_hook_on_world_begin_play, " handler").c_str());
  s7_define_function(s7session,
    name_ue_log, ue_log, 1, 0, false,
    function_help_string(name_ue_log, " string").c_str());
  s7_define_function(s7session,
    name_ue_material_instance_dynamic_set_scalar_parameter_value,
         ue_material_instance_dynamic_set_scalar_parameter_value,
    3, 0, false, function_help_string(
    name_ue_material_instance_dynamic_set_scalar_parameter_value,
      " instance name value").c_str());
  s7_define_function(s7session,
    name_umg_image_set_brush_from_texture,
         umg_image_set_brush_from_texture,
    3, 0, false, function_help_string(
    name_umg_image_set_brush_from_texture,
      " image texture match").c_str());
  s7_define_function(s7session,
    name_umg_panel_widget_get_child_at,
         umg_panel_widget_get_child_at,
    2, 0, false, function_help_string(
    name_umg_panel_widget_get_child_at,
      " panel index").c_str());
  s7_define_function(s7session,
    name_ue_primitive_component_add_force,
         ue_primitive_component_add_force,
    2, 0, false, function_help_string(
    name_ue_primitive_component_add_force,
      " component force").c_str());
  s7_define_function(s7session,
    name_ue_primitive_component_add_impulse,
         ue_primitive_component_add_impulse,
    2, 0, false, function_help_string(
    name_ue_primitive_component_add_impulse,
      " component impulse").c_str());
  s7_define_function(s7session,
    name_ue_primitive_component_get_material,
         ue_primitive_component_get_material,
    2, 0, false, function_help_string(
    name_ue_primitive_component_get_material,
      " component index").c_str());
  s7_define_function(s7session,
    name_ue_primitive_component_set_material,
         ue_primitive_component_set_material,
    3, 0, false, function_help_string(
    name_ue_primitive_component_set_material,
      " component index material").c_str());
  s7_define_function(s7session,
    name_ue_primitive_component_set_simulate_physics,
         ue_primitive_component_set_simulate_physics,
    2, 0, false, function_help_string(
    name_ue_primitive_component_set_simulate_physics,
      " component simulate").c_str());
  s7_define_function(s7session,
    name_ue_print_string, ue_print_string, 2, 0, false,
    function_help_string(name_ue_print_string, " world string)").c_str());
  s7_define_function(s7session,
    name_ue_print_string_primary, ue_print_string_primary, 1, 0, false,
    function_help_string(name_ue_print_string_primary, " string)").c_str());
  s7_define_function(s7session,
    name_ue_scene_component_find_skeletal_mesh,
         ue_scene_component_find_skeletal_mesh,
    1, 0, false, function_help_string(
    name_ue_scene_component_find_skeletal_mesh,
    " component").c_str());
  s7_define_function(s7session,
    name_ue_scene_component_set_visibility,
         ue_scene_component_set_visibility,
    2, 0, false, function_help_string(
    name_ue_scene_component_set_visibility,
    " component visible").c_str());
  s7_define_function(s7session,
    name_ue_uobject_get_class_name,
         ue_uobject_get_class_name,
    1, 0, false, function_help_string(
    name_ue_uobject_get_class_name,
      " uobject").c_str());
  s7_define_function(s7session,
    name_ue_uobject_get_display_name,
         ue_uobject_get_display_name,
    1, 0, false, function_help_string(
    name_ue_uobject_get_display_name,
      " uobject").c_str());
  s7_define_function(s7session,
    name_ue_vector_rotate_euler,
         ue_vector_rotate_euler,
    2, 0, false, function_help_string(
    name_ue_vector_rotate_euler,
      " vector angles").c_str());
  s7_define_function(s7session,
    name_ue_world_current_destroy_actor,
         ue_world_current_destroy_actor,
    1, 0, false, function_help_string(
    name_ue_world_current_destroy_actor,
      " actor").c_str());
  s7_define_function(s7session,
    name_ue_world_current_get_game_viewport,
         ue_world_current_get_game_viewport,
    0, 0, false, function_help_string(
    name_ue_world_current_get_game_viewport,
      "").c_str());
  s7_define_function(s7session,
    name_ue_world_current_spawn_actor,
         ue_world_current_spawn_actor,
    3, 0, false, function_help_string(
    name_ue_world_current_spawn_actor,
      " class location rotation").c_str());

  FString const scmPath = PluginSubpath(
    ANSI_TO_TCHAR("AboaUE"),
    ANSI_TO_TCHAR("Source/aboa"));
#if ALK_TRACING
  UE_LOG(LogAlkScheme, Display,
    TEXT("BEGIN listing scm path %s"), *scmPath);
  const FString result =
    FPlatformFileManager::Get().GetPlatformFile().IterateDirectory(
      *scmPath,
      [] (TCHAR const *const name, bool isDir) {
        UE_LOG(LogAlkScheme, Display, TEXT("%s"),
          *(FString(isDir ? "<D> " : "<F> ") +
            FPaths::GetCleanFilename(name)));
        return true;
      })
    ? "..END" : "FAILED";
  UE_LOG(LogAlkScheme, Display, TEXT("%s"), *result);
#endif
  AboaUeMutant const mutant = {{scmPath}, s7session};
  auto const code = loadAboaUeCode(
    FPaths::Combine(scmPath, TEXT("boot.aboa")));
  if (!code.source.IsEmpty()) {
    auto result = runAboaUeCode(mutant, code);
    UE_LOG(LogAlkScheme, Log, TEXT("Scheme session booted: %s"),
      *stringFromAboaUeDataDict(result, "result")
    );
  }
  return mutant;
}

auto loadAboaUeCode(FString const &path) -> AboaUeCode {
  FString mutSource;
  if (!FFileHelper::LoadFileToString(mutSource, *path, FFileHelper::EHashOptions::None))
    UE_LOG(LogAlkScheme, Error, TEXT("Failed to read %s"), *path)
  return {path, mutSource};
}

auto makeAboaUeResult(
  AboaUeMutant  const & mutant,
  s7_pointer    const & s7obj
) -> AboaUeDataDict {
  auto ref =
    s7_is_float_vector(s7obj)
    ? makeAboaUeDataVector( // TODO: ### ALLOCATED
        alloc_ue_vector_from_s7(s7obj))
    : (s7_is_vector(s7obj)
       && (s7_vector_length(s7obj) > 0)
       && s7_is_float_vector(s7_vector_elements(s7obj)[0]))
      ? makeAboaUeDataVectorArray( // TODO: ### ALLOCATED
          alloc_ue_vector_array_from_s7(mutant.s7session, s7obj))
      // ### TODO HANDLE OTHER TYPES
      : makeAboaUeDataString(
          *new FString(ANSI_TO_TCHAR(
            s7_object_to_c_string(mutant.s7session, s7obj))));
  return makeAboaUeDataDict({{"result", ref}});
}

auto callAboaUeCode(
  AboaUeMutant    const & mutant,
  FString         const & callee,
  AboaUeDataDict  const & args
) -> AboaUeDataDict {
  auto mutCallExpr = std::string("(") + TCHAR_TO_ANSI(*callee);
  std::stack<s7_int> mutProtectStack;
  for (auto & arg : args) {
    auto & key = arg.first;
    auto & ref = arg.second;
    s7_pointer s7value = s7_nil(mutant.s7session);
    switch (ref.type) {
      case AboaUeDataType::Bool : {
        // TODO: ### IMPLEMENT TYPE
        break;
      }
      case AboaUeDataType::Float : {
        auto fp = ueFloatPtrFromAny(ref.any);
        if (!fp) UE_LOG(LogAlkScheme, Error,
          TEXT("runAboaUeCode(...) arg type is not a float"))
        else
          s7value = s7_make_real(mutant.s7session, *fp);
        break;
      }
      case AboaUeDataType::MapNameUptr : {
        auto map = ueMapNameUptrFromAny(ref.any);
        if (!map) UE_LOG(LogAlkScheme, Error,
          TEXT("runAboaUeCode(...) arg type is not MapNameUptr"));
        s7value = s7_hash_table_from_ue_map_name_uptr(
          mutant.s7session, map ? *map : TMap<FName,TObjectPtr<UObject>>());
        break;
      }
      case AboaUeDataType::String : {
        // TODO: ### IMPLEMENT TYPE
        break;
      }
      case AboaUeDataType::UobjectPtr : {
        auto op = ueObjectPtrFromAny(ref.any);
        // TODO: ### value can be null, but also null if incorrect type
        s7value = s7_make_c_pointer(
          mutant.s7session, const_cast<UObject *>(op));
        break;
      }
      case AboaUeDataType::UobjectRef : {
        auto op = ueObjectPtrFromAny(ref.any);
        if (!op) UE_LOG(LogAlkScheme, Error,
          TEXT("runAboaUeCode(...) arg type is not a UobjectRef"))
        else
          s7value = s7_make_c_pointer(
            mutant.s7session, const_cast<UObject *>(op));
        break;
      }
      case AboaUeDataType::Vector : {
        auto vp = ueVectorPtrFromAny(ref.any);
        if (!vp) UE_LOG(LogAlkScheme, Error,
          TEXT("runAboaUeCode(...) arg type is not Vector"));
        s7value = scheme_ue_vector(
          mutant.s7session, vp ? *vp : FVector());
        break;
      }
      case AboaUeDataType::VectorArray : {
        auto vap = ueVectorArrayPtrFromAny(ref.any);
        if (!vap) UE_LOG(LogAlkScheme, Error,
          TEXT("runAboaUeCode(...) arg type is not VectorArray"));
        s7value = scheme_ue_vector_array(
          mutant.s7session, vap ? *vap : TArray<FVector>());
        break;
      }
    }
    //UE_LOG(LogAlkScheme, Error, TEXT("s7_define_variable %s %d"), *arg.first, &ref.any)
    auto argName = std::string("arg--") + TCHAR_TO_ANSI(*key);
    mutCallExpr += " " ;
    mutCallExpr += argName;
    mutProtectStack.push(
      s7_gc_protect(mutant.s7session,
        s7_define_constant(mutant.s7session, argName.c_str(), s7value)));
  }
  mutCallExpr += ')';
  auto result = makeAboaUeResult(mutant,
    s7_eval_c_string(mutant.s7session, mutCallExpr.c_str()));
  while (!mutProtectStack.empty()) {
    s7_gc_unprotect_at(mutant.s7session, mutProtectStack.top());
    mutProtectStack.pop();
  }
  return result;
}

auto runAboaUeCode(
  AboaUeMutant    const & mutant,
  AboaUeCode      const & code,
  FString         const & callee,
  AboaUeDataDict  const & args
) -> AboaUeDataDict {
  auto s7obj = s7_eval_c_string(
    mutant.s7session, TCHAR_TO_ANSI(*code.source));
  return callee.IsEmpty()
    ? makeAboaUeResult(mutant, s7obj)
    : callAboaUeCode(mutant, callee, args);
}

auto makeAboaUeDataDict(
  std::initializer_list<AboaUeDataArg> const & args
) -> AboaUeDataDict {
  auto dict = AboaUeDataDict();
  for (auto & arg : args)
    dict.emplace(std::make_pair(arg.name, arg.ref));
  return dict;
}

auto makeAboaUeDataFloat(float const & data) -> AboaUeDataRef {
  return {&data, AboaUeDataType::Float};
}

auto makeAboaUeDataMapNameUptr(
    TMap<FName,TObjectPtr<UObject>> const & data
) -> AboaUeDataRef {
  return {&data, AboaUeDataType::MapNameUptr};
}

auto makeAboaUeDataString(FString const & data) -> AboaUeDataRef {
  return {&data, AboaUeDataType::String};
}

auto makeAboaUeDataVector(FVector const & data) -> AboaUeDataRef {
  return {&data, AboaUeDataType::Vector};
}

auto makeAboaUeDataUobjectPtr(UObject const * data) -> AboaUeDataRef {
  return {data, AboaUeDataType::UobjectPtr};
}

auto makeAboaUeDataUobjectRef(UObject const & data) -> AboaUeDataRef {
  return {&data, AboaUeDataType::UobjectRef};
}

auto makeAboaUeDataVectorArray(TArray<FVector> const & data) -> AboaUeDataRef {
  return {&data, AboaUeDataType::VectorArray};
}

static void logErrorMap(
  char const * const   errorText,
  char const * const   callerName,
  FString      const & key
) {
  UE_LOG(LogAlkScheme, Error,
    TEXT("%s %s(map, \"%s\")"),
    ANSI_TO_TCHAR(errorText),
    ANSI_TO_TCHAR(callerName),
    *key);
}

static auto schemeUeDataRefInDict(
  char const *    const   callerName,
  AboaUeDataDict  const & dict,
  FString         const & key,
  AboaUeDataType          type
) -> AboaUeDataRef const * {
  auto iter = dict.find(key);
  if (iter == dict.end())
    logErrorMap("Failed to find", callerName, key);
  else if (iter->second.type != type)
    logErrorMap("Wrong type for", callerName, key);
  else
    return &iter->second;
  return nullptr;
}

auto floatFromAboaUeDataDict(
  AboaUeDataDict  const & dict,
  FString         const & key
) -> float {
  auto refOrNull = schemeUeDataRefInDict(
    "floatFromAboaUeDataDict", dict, key,
    AboaUeDataType::Float);
  if (refOrNull) {
    //UE_LOG(LogAlkScheme, Warning,
    //  TEXT("floatFromAboaUeDataDict ref->any has_value=%s, type=%s"),
    //  ANSI_TO_TCHAR(refOrNull->any.has_value() ? "true " : "false"),
    //  ANSI_TO_TCHAR(refOrNull->any.type().name()));
    auto fp = ueFloatPtrFromAny(refOrNull->any);
    if (fp)
      return *fp;
    else
      UE_LOG(LogAlkScheme, Error,
        TEXT("floatFromAboaUeDataDict(...) arg type is not float"));
  }
  return 0.f;
}

auto stringFromAboaUeDataDict(
  AboaUeDataDict  const & dict,
  FString         const & key
) -> FString {
  auto refOrNull = schemeUeDataRefInDict(
    "stringFromAboaUeDataDict", dict, key,
    AboaUeDataType::String);
  if (refOrNull) {
    //UE_LOG(LogAlkScheme, Warning,
    //  TEXT("stringFromAboaUeDataDict ref->any has_value=%s, type=%s"),
    //  ANSI_TO_TCHAR(refOrNull->any.has_value() ? "true " : "false"),
    //  ANSI_TO_TCHAR(refOrNull->any.type().name()));
    auto sp = ueStringPtrFromAny(refOrNull->any);
    if (sp)
      return *sp;
    else
      UE_LOG(LogAlkScheme, Error,
        TEXT("stringFromAboaUeDataDict(...) arg type is not String"));
  }
  return FString();
}

auto vectorArrayFromAboaUeDataDict(
  AboaUeDataDict const & dict,
  FString        const & key
) -> TArray<FVector> {
  auto refOrNull = schemeUeDataRefInDict(
    "vectorArrayFromAboaUeDataDict", dict, key,
    AboaUeDataType::VectorArray);
  if (refOrNull) {
    auto vap = ueVectorArrayPtrFromAny(refOrNull->any);
    if (vap)
      return *vap;
    else
      UE_LOG(LogAlkScheme, Error,
        TEXT("vectorArrayFromAboaUeDataDict(...) arg type is not VectorArray"));
  }
  return TArray<FVector>();
}

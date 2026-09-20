#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class USkeletalMesh;
class UStaticMesh;
class USoundBase;

/** Everything art/export imports to lives under /Game/IronHorse/<folder>. Loaded by name so nothing needs wiring in the editor. */
namespace FGAssets
{
    UStaticMesh* StaticMesh(const FString& Folder, const FString& Name);
    USkeletalMesh* SkeletalMesh(const FString& Folder, const FString& Name);
    /** The animation imported with MeshName whose name ends in _Action. */
    UAnimSequence* Anim(const FString& Folder, const FString& MeshName, const FString& Action);
    USoundBase* Sound(const FString& Name);

    struct FEntry { FString Path; FString Name; FString Class; };
    /** Everything under /Game/IronHorse, from Data/assets.json (Scripts/make_manifest.py). A packaged build's asset
        registry returns nothing for these folders, so names are looked up here, not there. */
    const TArray<FEntry>& Manifest();
}

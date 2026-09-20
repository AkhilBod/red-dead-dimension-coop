#include "FGAssets.h"

#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Sound/SoundBase.h"

namespace
{
    const TCHAR* Root = TEXT("/Game/IronHorse");

    template <typename T>
    T* LoadNamed(const FString& Folder, const FString& Name)
    {
        const FString Path = FString::Printf(TEXT("%s/%s/%s.%s"), Root, *Folder, *Name, *Name);
        T* Asset = LoadObject<T>(nullptr, *Path, nullptr, LOAD_NoWarn);
        if (!Asset)
        {
            UE_LOG(LogTemp, Warning, TEXT("IronHorse: missing %s. Run Scripts/import_art.py."), *Path);
        }
        return Asset;
    }

    TMap<FString, TWeakObjectPtr<UAnimSequence>> AnimCache;
}

UStaticMesh* FGAssets::StaticMesh(const FString& Folder, const FString& Name) { return LoadNamed<UStaticMesh>(Folder, Name); }
USkeletalMesh* FGAssets::SkeletalMesh(const FString& Folder, const FString& Name) { return LoadNamed<USkeletalMesh>(Folder, Name); }
USoundBase* FGAssets::Sound(const FString& Name) { return LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("%s/audio/%s.%s"), Root, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet); }

UAnimSequence* FGAssets::Anim(const FString& Folder, const FString& MeshName, const FString& Action)
{
    const FString Key = Folder / MeshName / Action;
    if (const TWeakObjectPtr<UAnimSequence>* Found = AnimCache.Find(Key))
    {
        if (Found->IsValid())
        {
            return Found->Get();
        }
    }
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    static bool bScanned = false;
    if (!bScanned)
    {
        // -game starts before the background asset scan finishes
        bScanned = true;
        Registry.ScanPathsSynchronous({ FString(Root) }, true);
    }
    TArray<FAssetData> Assets;
    Registry.GetAssetsByPath(FName(*FString::Printf(TEXT("%s/%s"), Root, *Folder)), Assets, false);
    // Interchange names takes "<Mesh><take>" ("SK_Banditaim_start", "SK_Train_BoxcarTrain_Boxcar_roll"),
    // and a file with a single take "<Mesh>_Anim". Shortest match wins, so "shoot" never picks "walk_shoot".
    const FAssetData* Best = nullptr;
    const FAssetData* OnlyTake = nullptr;
    for (const FAssetData& Data : Assets)
    {
        if (Data.AssetClassPath.GetAssetName() != TEXT("AnimSequence"))
        {
            continue;
        }
        FString Name = Data.AssetName.ToString();
        Name.RemoveFromStart(TEXT("A_"));
        if (!Name.RemoveFromStart(MeshName))
        {
            continue;
        }
        if (Name == TEXT("_Anim") || Name == TEXT("Anim"))
        {
            OnlyTake = &Data;
        }
        else if ((Name == Action || Name.EndsWith(TEXT("_") + Action)) && (!Best || Data.AssetName.ToString().Len() < Best->AssetName.ToString().Len()))
        {
            Best = &Data;
        }
    }
    if (!Best)
    {
        Best = OnlyTake;
    }
    UAnimSequence* Seq = Best ? Cast<UAnimSequence>(Best->GetAsset()) : nullptr;
    if (!Seq)
    {
        UE_LOG(LogTemp, Warning, TEXT("IronHorse: no animation %s for %s"), *Action, *MeshName);
    }
    AnimCache.Add(Key, Seq);
    return Seq;
}

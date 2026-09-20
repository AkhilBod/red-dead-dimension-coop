#include "FGAssets.h"

#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Sound/SoundBase.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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
    // Interchange names takes "<Mesh><take>" ("SK_Banditaim_start", "SK_Train_BoxcarTrain_Boxcar_roll"),
    // and a file with a single take "<Mesh>_Anim". Shortest match wins, so "shoot" never picks "walk_shoot".
    const FString FolderPath = FString::Printf(TEXT("%s/%s/"), Root, *Folder);
    const FEntry* Best = nullptr;
    const FEntry* OnlyTake = nullptr;
    for (const FEntry& Entry : Manifest())
    {
        if (Entry.Class != TEXT("AnimSequence") || !Entry.Path.StartsWith(FolderPath))
        {
            continue;
        }
        FString Name = Entry.Name;
        Name.RemoveFromStart(TEXT("A_"));
        if (!Name.RemoveFromStart(MeshName))
        {
            continue;
        }
        if (Name == TEXT("_Anim") || Name == TEXT("Anim"))
        {
            OnlyTake = &Entry;
        }
        else if ((Name == Action || Name.EndsWith(TEXT("_") + Action)) && (!Best || Entry.Name.Len() < Best->Name.Len()))
        {
            Best = &Entry;
        }
    }
    if (!Best)
    {
        Best = OnlyTake;
    }
    UAnimSequence* Seq = Best ? LoadObject<UAnimSequence>(nullptr, *(Best->Path + TEXT(".") + Best->Name), nullptr, LOAD_NoWarn) : nullptr;
    if (!Seq)
    {
        UE_LOG(LogTemp, Warning, TEXT("IronHorse: no animation %s for %s"), *Action, *MeshName);
    }
    AnimCache.Add(Key, Seq);
    return Seq;
}

const TArray<FGAssets::FEntry>& FGAssets::Manifest()
{
    static TArray<FEntry> Entries;
    static bool bLoaded = false;
    if (!bLoaded)
    {
        bLoaded = true;
        FString Text;
        TSharedPtr<FJsonObject> Json;
        const FString File = FPaths::ProjectContentDir() / TEXT("IronHorse/Data/assets.json");
        if (FFileHelper::LoadFileToString(Text, *File) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json)
        {
            for (const TSharedPtr<FJsonValue>& Value : Json->GetArrayField(TEXT("assets")))
            {
                const TSharedPtr<FJsonObject> Row = Value->AsObject();
                Entries.Add({ Row->GetStringField(TEXT("path")), Row->GetStringField(TEXT("name")), Row->GetStringField(TEXT("class")) });
            }
        }
        if (!Entries.Num())
        {
            UE_LOG(LogTemp, Error, TEXT("IronHorse: %s is missing or empty. Run Scripts/make_manifest.py."), *File);
        }
    }
    return Entries;
}

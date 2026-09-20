#include "FGWorldStreamer.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "FGAssets.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    constexpr double ChunkMetres = 50.0;
    // The train reaches 35 m back from the player; a chunk is dropped as soon as its far end is 70 m behind.
    // Ahead, the haze hides anything past ~400 m, so 450 m of track is all that ever needs to exist.
    constexpr double KeepBehind = 70.0;
    constexpr double KeepAhead = 450.0;

    // chunks.json is in Blender axes: metres, +Y = driver's left. Unreal: cm, +Y = right, yaw flips.
    FTransform FromJson(double X, double Y, double Z, double YawDeg)
    {
        return FTransform(FRotator(0.0, -YawDeg, 0.0), FVector(X * 100.0, -Y * 100.0, Z * 100.0));
    }
}

AFGWorldStreamer::AFGWorldStreamer()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    ChainRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ChainRoot"));
    ChainRoot->SetupAttachment(GetRootComponent());
    ChainRoot->SetMobility(EComponentMobility::Movable);
}

void AFGWorldStreamer::BeginPlay()
{
    Super::BeginPlay();
    Rng.GenerateNewSeed();
    LoadDefs();
}

void AFGWorldStreamer::LoadDefs()
{
    const FString Path = FPaths::ProjectContentDir() / TEXT("IronHorse/Data/chunks.json");
    FString Text;
    TSharedPtr<FJsonObject> RootObj;
    if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), RootObj) || !RootObj)
    {
        UE_LOG(LogTemp, Error, TEXT("IronHorse: cannot read %s"), *Path);
        return;
    }
    for (const TSharedPtr<FJsonValue>& Value : RootObj->GetArrayField(TEXT("chunks")))
    {
        const TSharedPtr<FJsonObject> C = Value->AsObject();
        FFGChunkDef& Def = Defs.AddDefaulted_GetRef();
        Def.Name = C->GetStringField(TEXT("name"));
        Def.StartJoint = C->GetStringField(TEXT("start_joint"));
        Def.EndJoint = C->GetStringField(TEXT("end_joint"));
        Def.Weight = C->GetNumberField(TEXT("weight"));
        const TSharedPtr<FJsonObject> E = C->GetObjectField(TEXT("end_connector"));
        Def.End = FromJson(E->GetNumberField(TEXT("x")), E->GetNumberField(TEXT("y")), E->GetNumberField(TEXT("z")), E->GetNumberField(TEXT("yaw_deg")));
        for (const TSharedPtr<FJsonValue>& P : C->GetArrayField(TEXT("centreline_every_5m")))
        {
            const TArray<TSharedPtr<FJsonValue>>& V = P->AsArray();
            Def.Centreline.Add(FVector(V[0]->AsNumber() * 100.0, -V[1]->AsNumber() * 100.0, -V[2]->AsNumber()));
        }
        for (const TSharedPtr<FJsonValue>& EvValue : C->GetArrayField(TEXT("events")))
        {
            const TSharedPtr<FJsonObject> Ev = EvValue->AsObject();
            FFGChunkEvent& Out = Def.Events.AddDefaulted_GetRef();
            Out.Kind = FName(*Ev->GetStringField(TEXT("kind")));
            if (Ev->HasField(TEXT("s")))
            {
                Out.S0 = Out.S1 = Ev->GetNumberField(TEXT("s"));
            }
            else
            {
                Out.S0 = Ev->GetNumberField(TEXT("s0"));
                Out.S1 = Ev->GetNumberField(TEXT("s1"));
            }
            Ev->TryGetStringField(TEXT("what"), Out.What);
        }
        for (const TSharedPtr<FJsonValue>& RigValue : C->GetArrayField(TEXT("rigs")))
        {
            const TSharedPtr<FJsonObject> R = RigValue->AsObject();
            FFGChunkRig& Out = Def.Rigs.AddDefaulted_GetRef();
            Out.Asset = R->GetStringField(TEXT("asset"));
            Out.Action = R->GetStringField(TEXT("action"));
            Out.Local = FromJson(R->GetNumberField(TEXT("x")), R->GetNumberField(TEXT("y")), R->GetNumberField(TEXT("z")), R->GetNumberField(TEXT("yaw_deg")));
        }
        for (const TSharedPtr<FJsonValue>& MValue : C->GetArrayField(TEXT("markers")))
        {
            const TSharedPtr<FJsonObject> M = MValue->AsObject();
            Def.Markers.Add(FName(*M->GetStringField(TEXT("name"))),
                FromJson(M->GetNumberField(TEXT("x")), M->GetNumberField(TEXT("y")), M->GetNumberField(TEXT("z")), M->GetNumberField(TEXT("yaw_deg"))));
        }
    }
    UE_LOG(LogTemp, Log, TEXT("IronHorse: %d chunk definitions"), Defs.Num());
}

const FFGChunkDef* AFGWorldStreamer::FindDef(const FString& QueuedName) const
{
    // "Flat_A+town" = that chunk, dressed as a town.
    FString ShortName = QueuedName;
    ShortName.RemoveFromEnd(TEXT("+town"));
    const FString Full = ShortName.StartsWith(TEXT("FG_Chunk_")) ? ShortName : TEXT("FG_Chunk_") + ShortName;
    return Defs.FindByPredicate([&Full](const FFGChunkDef& D) { return D.Name == Full; });
}

void AFGWorldStreamer::Queue(const TArray<FString>& Names)
{
    Pending.Append(Names);
}

const FFGChunkDef* AFGWorldStreamer::PickNext()
{
    const FString Joint = Chain.Num() ? Chain.Last().Def->EndJoint : TEXT("O");
    while (Pending.Num())
    {
        const FFGChunkDef* Def = FindDef(Pending[0]);
        if (Def && Def->StartJoint == Joint)
        {
            bNextIsTown = Pending[0].EndsWith(TEXT("+town"));
            Pending.RemoveAt(0);
            return Def;
        }
        if (!Def)
        {
            UE_LOG(LogTemp, Warning, TEXT("IronHorse: unknown chunk %s"), *Pending[0]);
            Pending.RemoveAt(0);
            continue;
        }
        break;      // queued chunk does not fit yet: lay something legal first
    }
    // Weighted random among legal followers. Inside a canyon/tunnel/side track with nothing queued, head for the way out.
    float Total = 0.0f;
    TArray<const FFGChunkDef*> Legal;
    for (const FFGChunkDef& D : Defs)
    {
        if (D.StartJoint != Joint) { continue; }
        if (Joint == TEXT("O") && D.EndJoint != TEXT("O")) { continue; }                 // set pieces only when the director asks
        if (D.Name.Contains(TEXT("Station"))) { continue; }
        if (!bAllowRandomLandmarks && D.Events.Num()) { continue; }
        if (bStraightOnly && !(D.End.GetRotation().IsIdentity(1e-4f) && FMath::IsNearlyZero(D.End.GetLocation().Y, 1.0))) { continue; }
        if (Chain.Num() && Chain.Last().Def == &D) { continue; }
        Legal.Add(&D);
        Total += D.Weight;
    }
    if (Joint != TEXT("O") && !Pending.Num())
    {
        for (const FFGChunkDef* D : Legal) { if (D->EndJoint == TEXT("O")) { return D; } }
    }
    float Roll = Rng.FRandRange(0.0f, Total);
    for (const FFGChunkDef* D : Legal)
    {
        Roll -= D->Weight;
        if (Roll <= 0.0f) { return D; }
    }
    return Legal.Num() ? Legal.Last() : (Defs.Num() ? &Defs[0] : nullptr);
}

void AFGWorldStreamer::Append(const FFGChunkDef* Def)
{
    FFGPlacedChunk& Placed = Chain.AddDefaulted_GetRef();
    Placed.Def = Def;
    if (Chain.Num() > 1)
    {
        const FFGPlacedChunk& Prev = Chain[Chain.Num() - 2];
        Placed.StartS = Prev.StartS + ChunkMetres;
        Placed.Start = Prev.Def->End * Prev.Start;
    }
    else
    {
        Placed.StartS = 0.0;
        Placed.Start = FTransform::Identity;
    }

    UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetStaticMesh(FGAssets::StaticMesh(TEXT("chunks"), TEXT("SM_") + Def->Name));
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetupAttachment(ChainRoot);
    Mesh->SetRelativeTransform(Placed.Start);
    if (Def->Name.Contains(TEXT("Tunnel_")) && !Def->Name.Contains(TEXT("Curve")))
    {
        // A third wider, so a full lean stays inside the walls. Only the straight pieces: widening a curve would
        // move its centreline off the one in chunks.json. The roof stays where it is: standing up in there is meant to hurt.
        // And a third taller. As modelled the inner cross-beams hang at 5.2 m, under even a ducked eye, and the view
        // went through every one of them. Dropped by the same factor's worth of rail height, so the rails still meet.
        Mesh->SetRelativeScale3D(FVector(1.0f, 1.3f, 1.3f));
        Mesh->AddRelativeLocation(FVector(0.0f, 0.0f, -53.0f * 0.3f));
    }
    if (bNextIsTown)
    {
        bNextIsTown = false;
        BuildTown(Placed);
    }
    Mesh->RegisterComponent();
    Placed.Mesh = Mesh;
    LiveMeshes.Add(Mesh);

    for (const FFGChunkRig& Rig : Def->Rigs)
    {
        const FString MeshName = TEXT("SK_") + Rig.Asset;
        USkeletalMesh* SkelMesh = FGAssets::SkeletalMesh(TEXT("setpieces"), MeshName);
        if (!SkelMesh) { continue; }
        USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(this);
        Comp->SetMobility(EComponentMobility::Movable);
        Comp->SetSkeletalMesh(SkelMesh);
        Comp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
        Comp->SetupAttachment(ChainRoot);
        Comp->SetRelativeTransform(Rig.Local * Placed.Start);
        Comp->RegisterComponent();
        Comp->ComponentTags.Add(FName(*Rig.Asset));
        if (UAnimSequence* Seq = FGAssets::Anim(TEXT("setpieces"), MeshName, Rig.Action))
        {
            // The bell waits to be shot. Everything else just runs.
            if (Rig.Asset != TEXT("StationBell"))
            {
                Comp->PlayAnimation(Seq, true);
            }
        }
        Placed.Rigs.Add(Comp);
        LiveRigs.Add(Comp);
    }
}

void AFGWorldStreamer::BuildTown(FFGPlacedChunk& Placed)
{
    // A street either side of the line, fronts to the track, 23 m out: clear of the riding lanes (6-18 m).
    // Buildings are modelled with the origin on the front wall and the front towards +Y.
    struct FLot { const TCHAR* Mesh; float HalfWidth; };
    static const FLot Lots[] = { {TEXT("SM_GeneralStore"), 490}, {TEXT("SM_Sheriff"), 390}, {TEXT("SM_Bank"), 440}, {TEXT("SM_Hotel"), 540},
                                 {TEXT("SM_Shack"), 290}, {TEXT("SM_Barn"), 540}, {TEXT("SM_Church"), 380}, {TEXT("SM_Outhouse"), 75} };
    static const TCHAR* Clutter[] = { TEXT("SM_Wagon"), TEXT("SM_HayBale"), TEXT("SM_Trough"), TEXT("SM_HitchingPost"), TEXT("SM_Bench"), TEXT("SM_WantedBoard"), TEXT("SM_Barrel"), TEXT("SM_Crate"), TEXT("SM_Signpost") };
    auto Place = [this, &Placed](const FString& Folder, const FString& Name, const FTransform& Local)
    {
        UStaticMesh* Asset = FGAssets::StaticMesh(Folder, Name);
        if (!Asset) { return; }
        UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
        Comp->SetMobility(EComponentMobility::Movable);
        Comp->SetStaticMesh(Asset);
        Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Comp->SetupAttachment(ChainRoot);
        Comp->SetRelativeTransform(Local * Placed.Start);
        Comp->RegisterComponent();
        Placed.Dressing.Add(Comp);
        LiveDressing.Add(Comp);
    };
    for (const float Side : { -1.0f, 1.0f })
    {
        const FRotator Face(0.0f, Side > 0.0f ? 180.0f : 0.0f, 0.0f);
        float X = 250.0f + Rng.FRandRange(0.0f, 300.0f);
        while (true)
        {
            const FLot& Lot = Lots[Rng.RandRange(0, UE_ARRAY_COUNT(Lots) - 1)];
            if (X + 2.0f * Lot.HalfWidth > 4800.0f) { break; }
            X += Lot.HalfWidth;
            Place(TEXT("buildings"), Lot.Mesh, FTransform(Face, FVector(X, Side * 2300.0f, 0.0f)));
            Place(TEXT("props"), Clutter[Rng.RandRange(0, UE_ARRAY_COUNT(Clutter) - 1)], FTransform(FRotator(0, Rng.FRandRange(0.f, 360.f), 0), FVector(X + Rng.FRandRange(-250.f, 250.f), Side * 2080.0f, 0.0f)));
            X += Lot.HalfWidth + Rng.FRandRange(120.0f, 380.0f);
            Place(TEXT("props"), TEXT("SM_LampPost"), FTransform(FVector(X - 60.0f, Side * 1980.0f, 0.0f)));
        }
    }
}

float AFGWorldStreamer::WorldYaw() const
{
    return ChainRoot->GetComponentRotation().Yaw;
}

void AFGWorldStreamer::DropFirst()
{
    FFGPlacedChunk& First = Chain[0];
    for (USceneComponent* Piece : First.Dressing)
    {
        LiveDressing.Remove(Piece);
        Piece->DestroyComponent();
    }
    if (First.Mesh)
    {
        LiveMeshes.Remove(First.Mesh);
        First.Mesh->DestroyComponent();
    }
    for (USkeletalMeshComponent* Rig : First.Rigs)
    {
        LiveRigs.Remove(Rig);
        Rig->DestroyComponent();
    }
    Chain.RemoveAt(0);
}

FTransform AFGWorldStreamer::ChainPose(double AtS) const
{
    if (!Chain.Num())
    {
        return FTransform(FVector(AtS * 100.0, 0.0, 0.0));
    }
    const FFGPlacedChunk* Chunk = &Chain[0];
    for (const FFGPlacedChunk& C : Chain)
    {
        if (AtS >= C.StartS) { Chunk = &C; }
    }
    const double U = AtS - Chunk->StartS;
    const TArray<FVector>& Line = Chunk->Def->Centreline;
    if (Line.Num() < 2 || U < 0.0 || U > ChunkMetres)
    {
        // Off either end of what is loaded: carry on straight.
        const FTransform Base = U < 0.0 ? Chunk->Start : Chunk->Def->End * Chunk->Start;
        const double Extra = U < 0.0 ? U : U - ChunkMetres;
        return FTransform(FVector(Extra * 100.0, 0.0, 0.0)) * Base;
    }
    const double F = U / 5.0;
    const int32 K = FMath::Clamp(int32(F), 0, Line.Num() - 2);
    const FVector P = FMath::Lerp(Line[K], Line[K + 1], F - K);
    return FTransform(FRotator(0.0, P.Z, 0.0), FVector(P.X, P.Y, 0.0)) * Chunk->Start;
}

void AFGWorldStreamer::SetDistance(double NewS)
{
    S = NewS;
    if (!Defs.Num()) { return; }
    while (!Chain.Num() || Chain.Last().StartS + ChunkMetres < S + KeepAhead)
    {
        const FFGChunkDef* Next = PickNext();
        if (!Next) { break; }
        Append(Next);
    }
    while (Chain.Num() > 1 && Chain[0].StartS + ChunkMetres < S - KeepBehind)
    {
        DropFirst();
    }
    ChainRoot->SetWorldTransform(ChainPose(S).Inverse());
}

FTransform AFGWorldStreamer::TrackWorld(double Ahead, float LateralCm) const
{
    return FTransform(FVector(0.0, LateralCm, 0.0)) * ChainPose(S + Ahead) * ChainRoot->GetComponentTransform();
}

float AFGWorldStreamer::MetresTo(FName Kind, FString* OutWhat) const
{
    float Best = -1.0f;
    for (const FFGPlacedChunk& C : Chain)
    {
        for (const FFGChunkEvent& E : C.Def->Events)
        {
            if (E.Kind != Kind) { continue; }
            const double A = C.StartS + E.S0 - S;
            const double B = C.StartS + E.S1 - S;
            if (B < 0.0) { continue; }
            const float D = A <= 0.0 ? 0.0f : float(A);
            if (Best < 0.0f || D < Best)
            {
                Best = D;
                if (OutWhat) { *OutWhat = E.What; }
            }
        }
    }
    return Best;
}

USkeletalMeshComponent* AFGWorldStreamer::FindRig(const FString& Asset) const
{
    for (USkeletalMeshComponent* Rig : LiveRigs)
    {
        if (Rig && Rig->ComponentHasTag(FName(*Asset))) { return Rig; }
    }
    return nullptr;
}

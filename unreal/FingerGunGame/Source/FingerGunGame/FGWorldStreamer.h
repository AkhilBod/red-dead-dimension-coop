#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FGWorldStreamer.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;

struct FFGChunkEvent
{
    FName Kind;          // duck, dark, side_track, trestle, narrow, station, overhead
    float S0 = 0.0f;     // metres along the chunk
    float S1 = 0.0f;
    FString What;
};

struct FFGChunkRig
{
    FString Asset;
    FString Action;
    FTransform Local;
};

struct FFGChunkDef
{
    FString Name;
    FString StartJoint;
    FString EndJoint;
    float Weight = 1.0f;
    FTransform End;                         // Connector_End in chunk space, Unreal axes, cm
    TArray<FVector> Centreline;             // x cm, y cm, yaw deg, every 5 m
    TArray<FFGChunkEvent> Events;
    TArray<FFGChunkRig> Rigs;
    TMap<FName, FTransform> Markers;
};

struct FFGPlacedChunk
{
    const FFGChunkDef* Def = nullptr;
    double StartS = 0.0;                    // metres along the whole line
    FTransform Start;                       // chain space
    TObjectPtr<UStaticMeshComponent> Mesh;
    TArray<TObjectPtr<USkeletalMeshComponent>> Rigs;
};

/**
 * The train never moves. This lays the 50 m chunks end to end in "chain space" (joint rule from chunks.json:
 * B may follow A when B.start_joint == A.end_joint) and every frame moves the whole chain by the inverse of
 * the track pose under the player, so curves swing the world round the train.
 */
UCLASS()
class FINGERGUNGAME_API AFGWorldStreamer : public AActor
{
    GENERATED_BODY()

public:
    AFGWorldStreamer();

    virtual void BeginPlay() override;

    /** Chunks to lay next, by name without the FG_Chunk_ prefix ("Landmark_WaterTower_A"). Random once empty. */
    void Queue(const TArray<FString>& Names);

    /** Metres travelled. Moves the world. */
    void SetDistance(double NewS);
    double GetDistance() const { return S; }

    /** World transform of the track S metres ahead of the player (negative = behind), Lateral cm to the right. */
    FTransform TrackWorld(double Ahead, float LateralCm = 0.0f) const;

    /** Nearest upcoming event of Kind: metres until it starts, or -1. If inside one, returns 0. */
    float MetresTo(FName Kind, FString* OutWhat = nullptr) const;
    bool Inside(FName Kind) const { return MetresTo(Kind) == 0.0f; }

    /** World transform of a rig (StationBell, WaterTower) in a live chunk, if any. */
    USkeletalMeshComponent* FindRig(const FString& Asset) const;

    bool bAllowRandomLandmarks = true;

private:
    UPROPERTY()
    TObjectPtr<USceneComponent> ChainRoot;

    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> LiveMeshes;

    UPROPERTY()
    TArray<TObjectPtr<USkeletalMeshComponent>> LiveRigs;

    TArray<FFGChunkDef> Defs;
    TArray<FFGPlacedChunk> Chain;
    TArray<FString> Pending;
    double S = 0.0;
    FRandomStream Rng;

    void LoadDefs();
    const FFGChunkDef* FindDef(const FString& ShortName) const;
    const FFGChunkDef* PickNext();
    void Append(const FFGChunkDef* Def);
    void DropFirst();
    FTransform ChainPose(double AtS) const;
};

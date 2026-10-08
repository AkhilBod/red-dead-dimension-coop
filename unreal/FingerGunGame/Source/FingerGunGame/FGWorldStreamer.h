#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FGNetTypes.h"
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
    int32 Index = 0;                        // since the line was laid
    double StartS = 0.0;                    // metres along the whole line
    FTransform Start;                       // chain space
    TObjectPtr<UStaticMeshComponent> Mesh;
    TArray<TObjectPtr<USkeletalMeshComponent>> Rigs;
    TArray<TObjectPtr<USceneComponent>> Dressing;
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

    virtual void PostInitializeComponents() override;

    /**
     * Co-op: on a client the line is not picked here but copied from the host, chunk by chunk (ApplyChunk), so both
     * screens show the same tunnels and bridges at the same distance.
     */
    bool bReplica = false;
    TFunction<void(const FFGChunkRec&)> OnChunkAppended;
    TFunction<void(int32 Id, double AtS, float Side)> OnLeanAdded;
    void ApplyChunk(const FFGChunkRec& Rec);
    void AddLeanObstacleAt(int32 Id, double AtS, float Side);
    int32 NextChunkIndex() const { return NextIndex; }
    /** Same chunks.json in the same order on both machines, or chunk numbers mean different things. */
    int32 DefsChecksum() const;

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

    /** A post beside the line with an arm out over half the roof, Ahead metres up the track. Side +1 = arm over the right half. */
    void AddLeanObstacle(double Ahead, float Side);
    /** Nearest one still ahead: metres to it, and which side its arm is on. -1 if none. */
    float MetresToLeanObstacle(float& OutSide) const;
    /** Did a signal arm sit between these two distances along the line? Each player is judged where they stand. */
    bool LeanObstacleBetween(double FromS, double ToS, float& OutSide) const;
    /** Did an event of Kind start between these two distances? */
    bool EventBetween(FName Kind, double FromS, double ToS) const;
    void ClearQueue() { Pending.Reset(); }
    /** Tear the whole line up and start again from nothing (ride again). */
    void ResetLine();
    /** Anything with an event (duck, dark, trestle, narrow, side track) within this many metres ahead? */
    bool EventsWithin(float Metres) const;

    /** Only straight chunks for now (the showdown: the sun has to stay put behind the boss). */
    bool bStraightOnly = false;

    /**
     * How much track to keep behind. The train reaches 35 m back from the player, so a chunk is normally dropped as
     * soon as its far end is 70 m behind. In a 1v1 one of the two faces backwards and sees as far as the other.
     */
    double KeepBehindM = 70.0;

    /** How far the world has been turned round the train, degrees. Anything that belongs to the landscape, like the sun, turns with it. */
    float WorldYaw() const;

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
    void Lay(const FFGChunkDef* Def, int32 Index, const FTransform& Start, int32 TownSeed);
    void BuildTown(FFGPlacedChunk& Placed, int32 Seed);

    struct FLeanObstacle { int32 Id; double S; float Side; TArray<TObjectPtr<USceneComponent>> Parts; };
    TArray<FLeanObstacle> LeanObstacles;
    void BuildLeanObstacle(FLeanObstacle& Ob);
    int32 NextTownSeed = 0;
    int32 NextIndex = 0;
    int32 NextLeanId = 0;

    UPROPERTY()
    TArray<TObjectPtr<USceneComponent>> LiveDressing;
    void DropFirst();
    FTransform ChainPose(double AtS) const;
};

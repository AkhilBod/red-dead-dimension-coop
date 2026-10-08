#pragma once

#include "CoreMinimal.h"
#include "FGNetTypes.generated.h"

UENUM()
enum class EFGPhase : uint8 { Title, Calibrate, Tutorial, Bell, Ride, Showdown, Result, Versus };

UENUM()
enum class EFGStage : uint8 { Riders, Boarders, SecondTrain };

UENUM()
enum class EFGBanditKind : uint8 { Rider, Boarder, TrainShooter, Dynamiter, Boss };

UENUM()
enum class EFGBanditState : uint8 { Entering, Idle, Telegraph, Recover, Leaving, Dead, Scripted };

UENUM()
enum class EFGAnchorKind : uint8 { None, OwnCar, BanditCar };

/** One-off effects every machine plays for itself, from a single multicast. */
UENUM()
enum class EFGFxKind : uint8 { Boom, DynamiteShot, DynamiteBlast, Dust, HitDust, Shards };

namespace FG
{
    constexpr float CruiseSpeed = 26.0f;        // m/s at the start of a run
    constexpr int32 ChunkRingSize = 32;         // 1.6 km of track decisions: far more than the 520 m that exist at once
    constexpr int32 LeanRingSize = 8;
}

/**
 * What something rides on. The world moves under a train that stays at the origin, and the cars swing on curves,
 * so a bandit on a boxcar is stored in that car's frame and every machine works out where the car is for itself.
 */
USTRUCT()
struct FFGAnchor
{
    GENERATED_BODY()

    UPROPERTY()
    EFGAnchorKind Kind = EFGAnchorKind::None;

    UPROPERTY()
    int8 Car = 0;

    static FFGAnchor OwnCar(int32 CarIndex) { FFGAnchor A; A.Kind = EFGAnchorKind::OwnCar; A.Car = int8(CarIndex); return A; }
    static FFGAnchor BanditCar(int32 CarIndex) { FFGAnchor A; A.Kind = EFGAnchorKind::BanditCar; A.Car = int8(CarIndex); return A; }
    bool IsSet() const { return Kind != EFGAnchorKind::None; }
};

/** A player's body and aim as everyone else needs it, packed small: sent about 30 times a second. */
USTRUCT()
struct FFGNetInput
{
    GENERATED_BODY()

    UPROPERTY()
    uint16 AimX = 32768;

    UPROPERTY()
    uint16 AimY = 32768;

    UPROPERTY()
    int8 Lean = 0;

    UPROPERTY()
    uint8 Duck = 0;

    /** Bits: aim valid, gun pose, holstered, tracking, tracker live. */
    UPROPERTY()
    uint8 Flags = 0;

    bool operator==(const FFGNetInput& O) const
    {
        return AimX == O.AimX && AimY == O.AimY && Lean == O.Lean && Duck == O.Duck && Flags == O.Flags;
    }
};

/** Where the train is along the line. Clients carry it forward between updates at Speed. */
USTRUCT()
struct FFGRideClock
{
    GENERATED_BODY()

    UPROPERTY()
    double S = 0.0;         // metres along the line since it was last reset

    UPROPERTY()
    float Speed = 0.0f;     // m/s

    UPROPERTY()
    double At = 0.0;        // server world time of this sample
};

/** One 50 m chunk the host laid. Clients lay the same one instead of rolling their own dice. */
USTRUCT()
struct FFGChunkRec
{
    GENERATED_BODY()

    UPROPERTY()
    int32 Epoch = -1;       // bumped by ride again: older records belong to a line that has been torn up

    UPROPERTY()
    int32 Index = -1;       // since the line was laid; its start is Index * 50 m along it

    UPROPERTY()
    int16 Def = -1;         // into chunks.json, which every copy of the game reads in the same order

    UPROPERTY()
    int32 TownSeed = 0;     // 0 = no town dressing

    UPROPERTY()
    FVector Start = FVector::ZeroVector;    // chain space, cm

    UPROPERTY()
    float StartYaw = 0.0f;
};

/** A signal arm over half the roof. */
USTRUCT()
struct FFGLeanRec
{
    GENERATED_BODY()

    UPROPERTY()
    int32 Epoch = -1;

    UPROPERTY()
    int32 Id = -1;

    UPROPERTY()
    double S = 0.0;

    UPROPERTY()
    int8 Side = 0;
};

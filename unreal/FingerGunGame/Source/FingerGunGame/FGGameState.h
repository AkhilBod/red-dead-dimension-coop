#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "FGNetTypes.h"
#include "GameFramework/GameStateBase.h"
#include "FGGameState.generated.h"

class AFGTrainPlayer;
class AFGPlayerState;

/**
 * The run as everyone sees it. The game mode (host only) decides everything and writes the result here every frame;
 * every machine, the host included, draws and plays from this. Timers are sent as server times, never as countdowns.
 */
UCLASS()
class FINGERGUNGAME_API AFGGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    AFGGameState();

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    double Now() const { return GetServerWorldTimeSeconds(); }

    UPROPERTY(Replicated)
    EFGPhase Phase = EFGPhase::Title;

    UPROPERTY(Replicated)
    uint8 ShowdownStep = 0;

    UPROPERTY(Replicated)
    double DrawCalledAt = 0.0;

    UPROPERTY(Replicated)
    int32 Lap = 0;

    UPROPERTY(Replicated)
    int32 BossesBeaten = 0;

    // the posse's totals
    UPROPERTY(Replicated)
    int32 Score = 0;

    UPROPERTY(Replicated)
    int32 Kills = 0;

    UPROPERTY(Replicated)
    int32 Headshots = 0;

    UPROPERTY(Replicated)
    int32 Dodges = 0;

    UPROPERTY(Replicated)
    int32 BestDrawMs = -1;

    UPROPERTY(Replicated)
    double DistanceM = 0.0;

    UPROPERTY(Replicated)
    FFGRideClock Clock;

    UPROPERTY(Replicated)
    FString Prompt;

    UPROPERTY(Replicated)
    FString SubPrompt;

    UPROPERTY(Replicated)
    FString Banner;

    UPROPERTY(Replicated)
    double BannerUntil = 0.0;

    UPROPERTY(Replicated)
    double ResultSince = 0.0;

    UPROPERTY(Replicated)
    bool bCrosshairVisible = false;

    UPROPERTY(Replicated)
    bool bStayDown = false;

    UPROPERTY(Replicated)
    bool bDuckWarning = false;

    UPROPERTY(Replicated)
    int8 LeanWarning = 0;           // -1 lean left, +1 lean right, 0 nothing coming

    UPROPERTY(Replicated)
    bool bTeamDown = false;

    /** How many rode this run: the result poster has a column each, and the ranks ask more of two. */
    UPROPERTY(Replicated)
    int32 Riders = 1;

    /** 1v1: the two players duel each other instead of riding together. */
    UPROPERTY(Replicated)
    bool bVersus = false;

    /** The host is waiting on the title for a partner to join (Enter rides alone). */
    UPROPERTY(Replicated)
    bool bWaitingForPartner = false;

    // sky
    UPROPERTY(Replicated)
    float Dusk = 0.0f;

    UPROPERTY(Replicated)
    float NightTarget = 0.0f;

    UPROPERTY(Replicated)
    float SunChainYaw = 150.0f;

    // the bandit train
    UPROPERTY(Replicated)
    bool bBanditTrainVisible = false;

    UPROPERTY(Replicated)
    float BanditTrainOffset = -400.0f;

    UPROPERTY(Replicated)
    bool bBellRaised = false;

    // the line, chunk by chunk (see AFGPresentation::FollowHost)
    UPROPERTY(Replicated)
    int32 LineEpoch = 0;

    UPROPERTY(Replicated)
    TArray<FFGChunkRec> ChunkRing;

    UPROPERTY(Replicated)
    TArray<FFGLeanRec> LeanRing;

    UPROPERTY(Replicated)
    int32 DefsChecksum = 0;

    void PushChunk(const FFGChunkRec& Rec);
    void PushLean(int32 Id, double AtS, float Side);

    float BannerLeft() const { return float(FMath::Max(0.0, BannerUntil - Now())); }
    float ResultTime() const { return Phase == EFGPhase::Result ? float(Now() - ResultSince) : 0.0f; }
    FString Rank() const;

    AFGTrainPlayer* PawnOf(const APlayerState* PS) const;

    // ---- everyone sees and hears it
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastSfx(FName Name, float Volume = 1.0f, float Pitch = 1.0f);

    UFUNCTION(NetMulticast, Unreliable)
    void MulticastFx(EFGFxKind Kind, FVector_NetQuantize10 At, float TrainSpeed);

    UFUNCTION(NetMulticast, Reliable)
    void MulticastEnemyShot(FVector_NetQuantize10 From, FVector_NetQuantize10 Target, float Flight);

    /** The shooter has already seen and heard their own shot, so their machine skips it. */
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastPlayerShot(AFGTrainPlayer* Shooter, FVector_NetQuantize10 Muzzle, FVector_NetQuantize10 HitPoint, uint8 Tracers, float Pitch);

    UFUNCTION(NetMulticast, Reliable)
    void MulticastHatOff(AFGTrainPlayer* Victim);

    UFUNCTION(NetMulticast, Reliable)
    void MulticastRingBell();
};

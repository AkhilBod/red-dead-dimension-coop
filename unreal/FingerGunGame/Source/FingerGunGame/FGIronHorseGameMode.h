#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FGIronHorseGameMode.generated.h"

class AFGBandit;
class AFGFx;
class AFGTarget;
class AFGTrain;
class AFGTrainPlayer;
class AFGWorldStreamer;
class ADirectionalLight;
class ASkyLight;
class UAudioComponent;
class UPointLightComponent;
struct FFGBanditSpec;

enum class EFGPhase : uint8 { Title, Calibrate, Tutorial, Bell, Ride, Showdown, Result };

struct FFGEnemyShot
{
    TWeakObjectPtr<AFGFx> Fx;
    FVector Target = FVector::ZeroVector;
    float TimeLeft = 0.0f;
    float Radius = 32.0f;
    bool bAccurate = true;
};

/**
 * The whole run from PLAN.md section 2: station tutorial (which is also the tracker calibration), a duck obstacle,
 * riders, boarders and a tunnel, the second train, the showdown, the result poster.
 * Put this game mode on any empty level and it builds everything itself.
 */
UCLASS()
class FINGERGUNGAME_API AFGIronHorseGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AFGIronHorseGameMode();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;

    // ---- player ----
    bool PlayerMayFire() const;
    bool HandleUiShot(FVector2D Aim);
    bool ResolvePlayerShot(const FVector& Origin, const FVector& Dir, const FVector& Muzzle);
    void OnPlayerDryFire();
    void OnPlayerReloaded();
    void PlaySfx(const FString& Name, float Volume = 1.0f, float Pitch = 1.0f);

    // ---- bandits ----
    bool RequestAttackToken();
    void ReleaseAttackToken();
    void OnTelegraph(AFGBandit* Bandit);
    void OnBanditKilled(AFGBandit* Bandit);
    void OnBossLanded();
    void SpawnEnemyShot(const FVector& From, bool bFast);
    void SpawnDynamite(const FVector& From);

    // ---- read by the HUD ----
    EFGPhase Phase = EFGPhase::Title;
    FString Prompt;
    FString SubPrompt;
    bool bCrosshairVisible = false;
    bool bDuckWarning = false;
    bool bPlayerDead = false;
    bool bBossBeaten = false;
    int32 Score = 0;
    int32 Kills = 0;
    int32 Headshots = 0;
    int32 Dodges = 0;
    float DrawTimeMs = -1.0f;
    float ResultTime = 0.0f;
    float HitMarker = 0.0f;
    float RideTime = 0.0f;
    float TrainSpeed = 0.0f;                // m/s
    FString Rank() const;
    float Accuracy() const;
    static FBox2D RideAgainButton() { return FBox2D(FVector2D(0.36, 0.76), FVector2D(0.64, 0.88)); }

    UPROPERTY()
    TObjectPtr<AFGTrainPlayer> Player;

    UPROPERTY()
    TObjectPtr<AFGWorldStreamer> World;

    UPROPERTY()
    TObjectPtr<AFGTrain> Train;

    UPROPERTY()
    TObjectPtr<AFGTrain> BanditTrain;

private:
    UPROPERTY()
    TArray<TObjectPtr<AFGBandit>> Bandits;

    UPROPERTY()
    TArray<TObjectPtr<AFGTarget>> Targets;

    UPROPERTY()
    TObjectPtr<AFGBandit> Boss;

    UPROPERTY()
    TObjectPtr<ADirectionalLight> Sun;

    UPROPERTY()
    TObjectPtr<ASkyLight> Sky;

    UPROPERTY()
    TObjectPtr<UAudioComponent> TrainLoop;

    UPROPERTY()
    TObjectPtr<UPointLightComponent> Lantern;

    TArray<FFGEnemyShot> Shots;
    int32 TokensOut = 0;
    float PhaseTime = 0.0f;
    float SpawnTimer = 0.0f;
    float SteamTimer = 0.0f;
    float InvulnerableFor = 0.0f;
    float LastDuckDistance = -1.0f;
    float Darkness = 0.0f;
    float SunIntensity = 7.0f;
    int32 CalibIndex = 0;
    int32 CansLeft = 0;
    int32 SpawnCount = 0;
    bool bQueuedTunnel = false;
    bool bQueuedSideTrack = false;
    bool bBanditTrainCrewed = false;
    // showdown
    int32 ShowdownStep = 0;
    float ShowdownTimer = 0.0f;
    float HolsteredFor = 0.0f;
    float HolsterWait = 0.0f;
    double DrawCalledAt = 0.0;

    // test switches, see BeginPlay
    bool bAutoPlay = false;
    bool bGod = false;
    float ShotEvery = 0.0f;
    float ShotTimer = 2.0f;
    float SkipTo = 0.0f;
    float AutoTimer = 3.0f;
    int32 ShotIndex = 0;
    void TickTest(float DeltaTime);

    void SetPhase(EFGPhase NewPhase);
    void BuildSky();
    void BuildTrains();
    void SpawnCalibBottle();
    void SpawnCans();
    void SpawnBell();
    void Depart();
    void TickRide(float DeltaTime);
    void TickShowdown(float DeltaTime);
    void TickShots(float DeltaTime);
    void TickDuck();
    void TickAtmosphere(float DeltaTime);
    void HurtPlayer(const TCHAR* Sfx);
    AFGBandit* SpawnBandit(const FFGBanditSpec& Spec);
    AFGTarget* SpawnTarget(const FString& Folder, const FString& Mesh, const FTransform& At, float Radius);
    int32 AliveBandits() const;
    void EveryoneLeave();
    FVector ScreenToWorldPoint(FVector2D Screen, float DistanceCm) const;
};

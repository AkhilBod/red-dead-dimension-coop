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
class AExponentialHeightFog;
struct FFGBanditSpec;

enum class EFGPhase : uint8 { Title, Calibrate, Tutorial, Bell, Ride, Showdown, Result };
enum class EFGStage : uint8 { Riders, Boarders, SecondTrain };

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
    /** On screen, inside a small margin. Bandits only shoot when the player could have seen it coming. */
    bool PlayerCanSee(const FVector& WorldPoint) const;
    void ReleaseAttackToken();
    void OnTelegraph(AFGBandit* Bandit);
    void OnBanditKilled(AFGBandit* Bandit);
    void OnBossLanded();
    void SpawnEnemyShot(const FVector& From, bool bFast);
    void SpawnDynamite(const FVector& From);

    // ---- difficulty: climbs with every lap and every minute, and levels off ----
    float TargetSpeed() const;          // m/s
    int32 MaxAlive() const;
    int32 TokenLimit() const;
    float ShotFlight() const;           // seconds an enemy bullet takes to arrive. Always dodgeable.
    float FireDelayScale() const;
    float SpawnEvery() const;

    // ---- read by the HUD ----
    int32 Lap = 0;                      // one lap = riders, boarders, the other train, a boss. Then again, harder.
    int32 BossesBeaten = 0;
    double DistanceM = 0.0;
    bool bStayDown = false;             // in a tunnel
    FString Banner;
    float BannerTime = 0.0f;
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
    /** Where the crosshair is drawn, 0..1: the tracker's aim, pulled toward the nearest thing worth shooting. */
    FVector2D AssistedAim = FVector2D(0.5, 0.5);
    FVector2D AssistedAim2 = FVector2D(0.5, 0.5);
    float RideTime = 0.0f;
    float TrainSpeed = 0.0f;                // m/s
    FString Rank() const;
    float Accuracy() const;
    const TArray<TObjectPtr<AFGBandit>>& AllBandits() const { return Bandits; }
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
    /** Every mesh, animation and sound of the game, loaded and built before play and never let go. See Preload(). */
    UPROPERTY()
    TArray<TObjectPtr<UObject>> Preloaded;

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
    TObjectPtr<AExponentialHeightFog> Fog;

    UPROPERTY()
    TObjectPtr<UAudioComponent> TrainLoop;

    UPROPERTY()
    TObjectPtr<UAudioComponent> Music[3];       // day, night, boss: all running, crossfaded by volume
    float MusicLevel[3] = { 0.0f, 0.0f, 0.0f };

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
    bool bInTunnel = false;
    bool bSkyCaptured = false;
    bool bQueuedSideTrack = false;
    bool bBanditTrainCrewed = false;
    // the endless run
    EFGStage Stage = EFGStage::Riders;
    float StageTime = 0.0f;
    float TrainTime = 0.0f;             // seconds the other train has been alongside
    int32 StageFlags = 0;
    int32 CrewSpawned = 0;
    int32 TestLap = 0;
    int32 TestStage = -1;
    // sky
    bool bOwnSky = false;
    float Dusk = 0.0f;                  // 0 golden hour .. 1 sun on the horizon, dead ahead
    float Night = 0.0f;
    float NightTarget = 0.0f;
    float SkyKey = -10.0f;
    float SunChainYaw = 150.0f;         // where the sun stands in the landscape, not relative to the train
    float QuietTime = 0.0f;             // seconds with nobody to shoot at
    TFunction<FTransform()> OnOwnCar(int32 CarIndex) const;
    // showdown
    int32 ShowdownStep = 0;
    float ShowdownTimer = 0.0f;
    float HolsteredFor = 0.0f;
    float HolsterWait = 0.0f;
    double DrawCalledAt = 0.0;

    // test switches, see BeginPlay
    bool bAutoPlay = false;
    bool bGod = false;
    bool bPerf = false;
    float PerfSum = 0.0f;
    float PerfWorst = 0.0f;
    int32 PerfSlow = 0;
    int32 PerfFrames = 0;
    float ShotEvery = 0.0f;
    float ShotTimer = 2.0f;
    float SkipTo = 0.0f;
    float AutoTimer = 3.0f;
    int32 ShotIndex = 0;
    void TickTest(float DeltaTime);

    void SetPhase(EFGPhase NewPhase);
    void Preload();
    void BuildSky();
    void BuildTrains();
    void SpawnCalibBottle();
    void SpawnCans();
    void SpawnBell();
    void Depart();
    void BeginLap(int32 NewLap);
    void NextStage(EFGStage NewStage);
    void SpawnBarrel(const FVector& Local, TFunction<FTransform()> Anchor);
    void TickRide(float DeltaTime);
    void TickShowdown(float DeltaTime);
    void TickShots(float DeltaTime);
    void TickDuck();
    void TickAtmosphere(float DeltaTime);
    void TickMagnet(float DeltaTime);
    void ShootablePoints(TArray<FVector>& Out) const;
    FVector2D MagnetOffset = FVector2D::ZeroVector;
    FVector2D MagnetOffset2 = FVector2D::ZeroVector;
    FVector2D Magnet(FVector2D Raw, FVector2D& Offset, const TArray<FVector>& Points, float RealDelta) const;
    void HurtPlayer(const TCHAR* Sfx);
    AFGBandit* SpawnBandit(const FFGBanditSpec& Spec);
    AFGTarget* SpawnTarget(const FString& Folder, const FString& Mesh, const FTransform& At, float Radius);
    int32 AliveBandits() const;
    void EveryoneLeave();
    FVector ScreenToWorldPoint(FVector2D Screen, float DistanceCm) const;
};

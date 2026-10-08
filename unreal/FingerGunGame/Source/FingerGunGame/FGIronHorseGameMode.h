#pragma once

#include "CoreMinimal.h"
#include "FGNetTypes.h"
#include "GameFramework/GameModeBase.h"
#include "FGIronHorseGameMode.generated.h"

class AFGBandit;
class AFGGameState;
class AFGPlayerState;
class AFGPresentation;
class AFGTarget;
class AFGTrain;
class AFGTrainPlayer;
class AFGWorldStreamer;
class APlayerController;
class UStaticMesh;
struct FFGBanditSpec;

struct FFGEnemyShot
{
    TWeakObjectPtr<AFGTrainPlayer> Victim;
    FVector Target = FVector::ZeroVector;
    float TimeLeft = 0.0f;          // until it is judged
    float Radius = 32.0f;
    bool bAccurate = true;
};

/**
 * The whole run from PLAN.md section 2: station tutorial (which is also the tracker calibration), a duck obstacle,
 * riders, boarders and a tunnel, the second train, the showdown, the result poster.
 * Put this game mode on any empty level and it builds everything itself.
 *
 * Co-op: this runs on the host only and decides everything for both riders. What everyone needs to see goes into
 * AFGGameState at the end of each frame (PublishState).
 */
UCLASS()
class FINGERGUNGAME_API AFGIronHorseGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AFGIronHorseGameMode();

    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal) override;
    virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
    virtual void Logout(AController* Exiting) override;

    // ---- players
    TArray<AFGTrainPlayer*> Players() const;
    TArray<AFGTrainPlayer*> AlivePlayers() const;
    bool PlayerMayFire(const AFGTrainPlayer* P) const;
    /** Back to the station with a clean slate, in place: no level reload to go wrong. */
    void RideAgain();
    void VoteRideAgain(APlayerController* PC);
    /** The host stops waiting for a partner. */
    void RideAlone();
    /** From the main menu: the solo run. */
    void MenuRide();
    bool ResolvePlayerShot(AFGTrainPlayer* P, const FVector& Origin, const FVector& Dir, const FVector& Muzzle);
    void PlaySfx(FName Name, float Volume = 1.0f, float Pitch = 1.0f);

    // ---- bandits
    /** Someone who can see this point and has an attack token free (it is taken), or nobody. */
    AFGTrainPlayer* PickTarget(const FVector& WorldPoint);
    void ReleaseAttackToken(AFGTrainPlayer* P);
    void OnTelegraph(AFGBandit* Bandit);
    void OnBanditKilled(AFGBandit* Bandit);
    void OnBossLanded();
    void SpawnEnemyShot(const FVector& From, bool bFast, AFGTrainPlayer* Victim, float ExtraDelay);
    void SpawnDynamite(const FVector& From, AFGTrainPlayer* Victim);

    // ---- difficulty: climbs with every lap and every minute, and levels off
    float TargetSpeed() const;          // m/s
    int32 MaxAlive() const;
    int32 TokenLimit() const;
    float ShotFlight() const;           // seconds an enemy bullet takes to arrive. Always dodgeable.
    float FireDelayScale() const;
    float SpawnEvery() const;
    /** Two guns on the roof: a few more bandits, coming a little faster. */
    float CoopScale() const;

    int32 Lap = 0;                      // one lap = riders, boarders, the other train, a boss. Then again, harder.
    int32 BossesBeaten = 0;
    double DistanceM = 0.0;
    bool bStayDown = false;             // in a tunnel
    float LeanWarning = 0.0f;           // -1 lean left, +1 lean right, 0 nothing coming
    EFGPhase Phase = EFGPhase::Title;
    FString Prompt;
    FString SubPrompt;
    bool bCrosshairVisible = false;
    bool bDuckWarning = false;
    bool bTeamDown = false;
    bool bBossBeaten = false;
    int32 Score = 0;
    int32 Kills = 0;
    int32 Headshots = 0;
    int32 Dodges = 0;
    float DrawTimeMs = -1.0f;
    float ResultTime = 0.0f;
    float RideTime = 0.0f;
    float TrainSpeed = 0.0f;                // m/s

    UPROPERTY()
    TObjectPtr<AFGWorldStreamer> World;

    UPROPERTY()
    TObjectPtr<AFGTrain> Train;

    UPROPERTY()
    TObjectPtr<AFGTrain> BanditTrain;

private:
    UPROPERTY()
    TObjectPtr<AFGPresentation> Presentation;

    UPROPERTY()
    TArray<TObjectPtr<AFGBandit>> Bandits;

    UPROPERTY()
    TArray<TObjectPtr<AFGTarget>> Targets;

    UPROPERTY()
    TObjectPtr<AFGBandit> Boss;

    AFGGameState* GS() const;
    AFGTrainPlayer* LocalPlayer() const;
    void AddScore(int32 Points, AFGTrainPlayer* By);
    void AddDodge(AFGTrainPlayer* P, int32 Points);
    void PlaySfxFor(AFGTrainPlayer* P, FName Name, float Volume = 1.0f, float Pitch = 1.0f);
    float JudgeDelay(const AFGTrainPlayer* P) const;
    float DrawGrace() const;
    void ShowBanner(const FString& Text, float Seconds);
    void PublishState();

    TArray<FFGEnemyShot> Shots;
    float PhaseTime = 0.0f;
    float SpawnTimer = 0.0f;
    int32 CalibIndex = 0;
    int32 CansLeft = 0;
    int32 SpawnCount = 0;
    bool bInTunnel = false;
    bool bBanditTrainCrewed = false;
    bool bRideAlone = false;
    // 1v1 (?versus): the two players face each other across the roof and quick-draw. Lose three hats, lose the duel.
    bool bVersus = false;
    int32 VersusStep = 0;               // 0 intro, 1 holster, 2 wait, 3 DRAW!, 4 after a round
    float VersusTimer = 0.0f;
    TWeakObjectPtr<AFGTrainPlayer> PendingWinner;
    int32 PendingMs = 0;
    double PendingUntil = 0.0;
    void StartVersus();
    void TickVersus(float DeltaTime);
    bool ResolveVersusShot(AFGTrainPlayer* P, const FVector& Origin, const FVector& Dir, const FVector& Muzzle);
    void WinRound(AFGTrainPlayer* Winner, int32 Ms, bool bFoul);
    AFGTrainPlayer* Opponent(const AFGTrainPlayer* P) const;
    bool bWaitingForPartner = false;
    int32 LastPlayerCount = 0;
    // the endless run
    EFGStage Stage = EFGStage::Riders;
    float StageTime = 0.0f;
    float TrainTime = 0.0f;             // seconds the other train has been alongside
    int32 StageFlags = 0;
    int32 CrewSpawned = 0;
    int32 TestLap = 0;
    int32 TestStage = -1;
    // sky
    float Dusk = 0.0f;                  // 0 golden hour .. 1 sun on the horizon, dead ahead
    float NightTarget = 0.0f;
    float SunChainYaw = 150.0f;         // where the sun stands in the landscape, not relative to the train
    float QuietTime = 0.0f;
    float ObstacleTimer = 12.0f;
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
    int32 ShotIndex = 0;
    void TickTest(float DeltaTime);

    void SetPhase(EFGPhase NewPhase);
    void SpawnCalibBottle();
    void SpawnCans();
    void SpawnBell();
    void Depart();
    void BeginLap(int32 NewLap);
    void NextStage(EFGStage NewStage);
    void SpawnBarrel(const FVector& Local, const FFGAnchor& Anchor);
    void TickTitle();
    void TickRide(float DeltaTime);
    void TickShowdown(float DeltaTime);
    void TickShots(float DeltaTime);
    void TickHazards();
    void TickLeanObstacles(float DeltaTime);
    void TickSky(float DeltaTime);
    void HurtPlayer(AFGTrainPlayer* P, FName Sfx);
    void Revive(AFGTrainPlayer* P, int32 WithHats);
    bool CanSee(const AFGTrainPlayer* P, const FVector& WorldPoint) const;
    AFGBandit* SpawnBandit(const FFGBanditSpec& Spec);
    AFGTarget* SpawnTarget(UStaticMesh* Mesh, const FTransform& At, float Radius);
    int32 AliveBandits() const;
    void EveryoneLeave();
    FVector ScreenToWorldPoint(FVector2D Screen, float DistanceCm) const;
};

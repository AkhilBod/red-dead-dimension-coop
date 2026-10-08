#pragma once

#include "CoreMinimal.h"
#include "FGNetTypes.h"
#include "GameFramework/Actor.h"
#include "FGPresentation.generated.h"

class AFGGameState;
class AFGTrain;
class AFGWorldStreamer;
class ADirectionalLight;
class AExponentialHeightFog;
class ASkyLight;
class UAudioComponent;
class UPointLightComponent;
class USkeletalMeshComponent;

/**
 * Everything you see and hear that is not a bandit or a player: the line under the train, both trains, sky, fog,
 * lantern, steam, the train's rumble and the music. One per machine, never replicated.
 * On the host (and in solo) the game mode drives the line and this only dresses it. On a co-op client it copies the
 * host's line from the game state and carries the train forward between updates.
 */
UCLASS()
class FINGERGUNGAME_API AFGPresentation : public AActor
{
    GENERATED_BODY()

public:
    AFGPresentation();

    /** The one in this world, built on first use. */
    static AFGPresentation* Get(const UObject* WorldContext);

    virtual void Tick(float DeltaTime) override;

    /** Where something riding on Anchor is: the origin, a car of our train, or a car of the bandit train. */
    FTransform AnchorTransform(const FFGAnchor& Anchor) const;

    /** Has the line under a co-op client caught up with the host's? */
    bool IsLineReady() const;

    void RingBell();

    UPROPERTY()
    TObjectPtr<AFGWorldStreamer> World;

    UPROPERTY()
    TObjectPtr<AFGTrain> Train;

    UPROPERTY()
    TObjectPtr<AFGTrain> BanditTrain;

    float Speed() const { return CurrentSpeed; }

    static constexpr float PlayerForwardCm = 300.0f;    // where the solo player stands on the passenger car roof
    static constexpr float SideTrackCm = -700.0f;       // the enemy line is 7 m to the driver's left

private:
    void Build();
    void BuildSky();
    void BuildTrains();
    void FollowHost(AFGGameState* GS, float DeltaTime);
    void TickSound(AFGGameState* GS, float DeltaTime);
    void TickSky(AFGGameState* GS, float DeltaTime);
    void TickTest(AFGGameState* GS, float DeltaTime);

    // test switches: -FGPerf logs frame times every 2 s, -FGShots=4 saves a screenshot every 4 s
    bool bPerf = false;
    float PerfSum = 0.0f;
    float PerfWorst = 0.0f;
    int32 PerfSlow = 0;
    int32 PerfFrames = 0;
    float ShotEvery = 0.0f;
    float ShotTimer = 2.0f;
    int32 ShotIndex = 0;

    bool bAuthority = true;
    float CurrentSpeed = 0.0f;

    // co-op client: following the host's line
    int32 Epoch = -1;
    double LocalS = 0.0;
    bool bHaveS = false;
    int32 DefsWarned = 0;

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

    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> RaisedBell;

    float SteamTimer = 0.0f;
    float Darkness = 0.0f;
    float SunIntensity = 7.0f;
    float Night = 0.0f;
    float SkyKey = -10.0f;
    int32 SkyEpoch = -1;
    bool bOwnSky = false;
    bool bSkyCaptured = false;
};

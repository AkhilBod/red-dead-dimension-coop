#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FGSession.generated.h"

class UNetDriver;

/**
 * Lives as long as the game does, across hosting and joining (each of which loads the level again).
 * Holds every asset loaded once (so a join is not a two minute stall), and hosts, joins and reports why a join failed.
 */
UCLASS()
class FINGERGUNGAME_API UFGSession : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    static UFGSession* Get(const UObject* WorldContext);

    /**
     * Running from the editor binary, an asset is built the first time it is loaded (render data, skinning, distance
     * fields: 5-7 s each for a chunk or a cowboy) and the game thread waits for it. Mid-run that was a multi-second
     * freeze whenever a new chunk type or enemy type appeared. So: load all of it once, wait for the builds, keep it.
     */
    void Preload();

    /** Reopen the level as a listen server on UDP 7777. Versus: a 1v1 quick-draw duel against the partner. */
    void Host(const UObject* WorldContext, bool bVersus);
    /** Back to the main menu, solo. In co-op this leaves the ride. */
    void Leave(const UObject* WorldContext);
    /** Travel to a host. Address is an IP, optionally with :port. */
    void Join(const UObject* WorldContext, const FString& Address);

    /** What went wrong with the last connection, shown on the title screen for a while. */
    FString LastError;
    double LastErrorAt = -1000.0;
    FString JoiningAddress;

    /** This machine's address on the local network, for the host to read out to a friend. */
    static FString LocalAddress();

private:
    UPROPERTY()
    TArray<TObjectPtr<UObject>> Preloaded;
    bool bPreloaded = false;

    FDelegateHandle NetworkFailureHandle;
    FDelegateHandle TravelFailureHandle;
    void OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Message);
    void OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Message);
    void Fail(const FString& Message);
};

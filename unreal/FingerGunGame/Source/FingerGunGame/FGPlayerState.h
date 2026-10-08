#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "FGPlayerState.generated.h"

/** One rider's own tally, for their column on the result poster. */
UCLASS()
class FINGERGUNGAME_API AFGPlayerState : public APlayerState
{
    GENERATED_BODY()

public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** 0 = the host's spot at the front of the roof, 1 = the partner's, behind and to the right. */
    UPROPERTY(Replicated)
    int32 Seat = 0;

    UPROPERTY(Replicated)
    int32 Points = 0;

    UPROPERTY(Replicated)
    int32 Kills = 0;

    UPROPERTY(Replicated)
    int32 Headshots = 0;

    UPROPERTY(Replicated)
    int32 Dodges = 0;

    UPROPERTY(Replicated)
    int32 ShotsFired = 0;

    UPROPERTY(Replicated)
    int32 ShotsHit = 0;

    UPROPERTY(Replicated)
    int32 TimesDowned = 0;

    UPROPERTY(Replicated)
    int32 BestDrawMs = -1;

    /** 1v1: rounds lost for firing before DRAW. (Rounds won are Points.) */
    UPROPERTY(Replicated)
    int32 Fouls = 0;

    /** Asked to ride again from the result poster. */
    UPROPERTY(Replicated)
    bool bVotedRide = false;

    float Accuracy() const { return ShotsFired > 0 ? float(ShotsHit) / float(ShotsFired) : 0.0f; }
    void ResetTally();
};

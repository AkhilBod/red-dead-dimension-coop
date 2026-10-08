#include "FGPlayerState.h"

#include "Net/UnrealNetwork.h"

void AFGPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFGPlayerState, Seat);
    DOREPLIFETIME(AFGPlayerState, Points);
    DOREPLIFETIME(AFGPlayerState, Kills);
    DOREPLIFETIME(AFGPlayerState, Headshots);
    DOREPLIFETIME(AFGPlayerState, Dodges);
    DOREPLIFETIME(AFGPlayerState, ShotsFired);
    DOREPLIFETIME(AFGPlayerState, ShotsHit);
    DOREPLIFETIME(AFGPlayerState, TimesDowned);
    DOREPLIFETIME(AFGPlayerState, BestDrawMs);
    DOREPLIFETIME(AFGPlayerState, Fouls);
    DOREPLIFETIME(AFGPlayerState, bVotedRide);
}

void AFGPlayerState::ResetTally()
{
    Points = Kills = Headshots = Dodges = ShotsFired = ShotsHit = TimesDowned = Fouls = 0;
    BestDrawMs = -1;
    bVotedRide = false;
}

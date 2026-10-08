#include "FGGameState.h"

#include "FGCombat.h"
#include "FGPresentation.h"
#include "FGSession.h"
#include "FGTrainPlayer.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

AFGGameState::AFGGameState()
{
    SetNetUpdateFrequency(30.0f);
    ChunkRing.SetNum(FG::ChunkRingSize);
    LeanRing.SetNum(FG::LeanRingSize);
}

void AFGGameState::BeginPlay()
{
    Super::BeginPlay();
    // A co-op client gets here with no game mode: load everything now, then build its own copy of the world.
    if (UFGSession* Session = UFGSession::Get(this)) { Session->Preload(); }
    AFGPresentation::Get(this);
}

void AFGGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFGGameState, Phase);
    DOREPLIFETIME(AFGGameState, ShowdownStep);
    DOREPLIFETIME(AFGGameState, DrawCalledAt);
    DOREPLIFETIME(AFGGameState, Lap);
    DOREPLIFETIME(AFGGameState, BossesBeaten);
    DOREPLIFETIME(AFGGameState, Score);
    DOREPLIFETIME(AFGGameState, Kills);
    DOREPLIFETIME(AFGGameState, Headshots);
    DOREPLIFETIME(AFGGameState, Dodges);
    DOREPLIFETIME(AFGGameState, BestDrawMs);
    DOREPLIFETIME(AFGGameState, DistanceM);
    DOREPLIFETIME(AFGGameState, Clock);
    DOREPLIFETIME(AFGGameState, Prompt);
    DOREPLIFETIME(AFGGameState, SubPrompt);
    DOREPLIFETIME(AFGGameState, Banner);
    DOREPLIFETIME(AFGGameState, BannerUntil);
    DOREPLIFETIME(AFGGameState, ResultSince);
    DOREPLIFETIME(AFGGameState, bCrosshairVisible);
    DOREPLIFETIME(AFGGameState, bStayDown);
    DOREPLIFETIME(AFGGameState, bDuckWarning);
    DOREPLIFETIME(AFGGameState, LeanWarning);
    DOREPLIFETIME(AFGGameState, bTeamDown);
    DOREPLIFETIME(AFGGameState, Riders);
    DOREPLIFETIME(AFGGameState, bWaitingForPartner);
    DOREPLIFETIME(AFGGameState, bVersus);
    DOREPLIFETIME(AFGGameState, Dusk);
    DOREPLIFETIME(AFGGameState, NightTarget);
    DOREPLIFETIME(AFGGameState, SunChainYaw);
    DOREPLIFETIME(AFGGameState, bBanditTrainVisible);
    DOREPLIFETIME(AFGGameState, BanditTrainOffset);
    DOREPLIFETIME(AFGGameState, bBellRaised);
    DOREPLIFETIME(AFGGameState, LineEpoch);
    DOREPLIFETIME(AFGGameState, ChunkRing);
    DOREPLIFETIME(AFGGameState, LeanRing);
    DOREPLIFETIME(AFGGameState, DefsChecksum);
}

void AFGGameState::PushChunk(const FFGChunkRec& Rec)
{
    // A ring: one slot changes per chunk, so only that slot goes over the wire.
    FFGChunkRec& Slot = ChunkRing[Rec.Index % FG::ChunkRingSize];
    Slot = Rec;
    Slot.Epoch = LineEpoch;
}

void AFGGameState::PushLean(int32 Id, double AtS, float Side)
{
    FFGLeanRec& Slot = LeanRing[Id % FG::LeanRingSize];
    Slot.Epoch = LineEpoch;
    Slot.Id = Id;
    Slot.S = AtS;
    Slot.Side = int8(Side);
}

FString AFGGameState::Rank() const
{
    // Two guns earn more than one, so two riders need more for the same title.
    const float Scale = Riders > 1 ? 1.6f : 1.0f;
    if (Score >= 9000 * Scale) { return TEXT("LEGEND"); }
    if (Score >= 5500 * Scale) { return TEXT("BOUNTY HUNTER"); }
    if (Score >= 3200 * Scale) { return TEXT("GUNSLINGER"); }
    if (Score >= 1900 * Scale) { return TEXT("DEPUTY"); }
    if (Score >= 900 * Scale) { return TEXT("DRIFTER"); }
    return TEXT("GREENHORN");
}

AFGTrainPlayer* AFGGameState::PawnOf(const APlayerState* PS) const
{
    return PS ? Cast<AFGTrainPlayer>(PS->GetPawn()) : nullptr;
}

void AFGGameState::MulticastSfx_Implementation(FName Name, float Volume, float Pitch)
{
    FGCombat::Sfx(GetWorld(), Name, Volume, Pitch);
}

void AFGGameState::MulticastFx_Implementation(EFGFxKind Kind, FVector_NetQuantize10 At, float TrainSpeed)
{
    FGCombat::Fx(GetWorld(), Kind, At, TrainSpeed);
}

void AFGGameState::MulticastEnemyShot_Implementation(FVector_NetQuantize10 From, FVector_NetQuantize10 Target, float Flight)
{
    FGCombat::EnemyShot(GetWorld(), From, Target, Flight);
    FGCombat::Sfx(GetWorld(), TEXT("shot_enemy"), 0.9f);
}

void AFGGameState::MulticastPlayerShot_Implementation(AFGTrainPlayer* Shooter, FVector_NetQuantize10 Muzzle, FVector_NetQuantize10 HitPoint, uint8 Tracers, float Pitch)
{
    if (!Shooter || Shooter->IsLocallyControlled()) { return; }
    Shooter->RemoteShot();
    FGCombat::PlayerShot(GetWorld(), Muzzle, HitPoint, Tracers);
    FGCombat::Sfx(GetWorld(), TEXT("shot_player"), 0.7f, Pitch);
}

void AFGGameState::MulticastHatOff_Implementation(AFGTrainPlayer* Victim)
{
    if (Victim) { FGCombat::HatOff(GetWorld(), Victim->HeadLocation()); }
}

void AFGGameState::MulticastRingBell_Implementation()
{
    if (AFGPresentation* P = AFGPresentation::Get(this)) { P->RingBell(); }
}

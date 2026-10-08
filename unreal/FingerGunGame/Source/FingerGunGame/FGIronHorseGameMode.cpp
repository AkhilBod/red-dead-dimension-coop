#include "FGIronHorseGameMode.h"

#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FGAssets.h"
#include "FGBandit.h"
#include "FGCombat.h"
#include "FGGameState.h"
#include "FGHud.h"
#include "FGPlayerController.h"
#include "FGPlayerState.h"
#include "FGPresentation.h"
#include "FGSession.h"
#include "FGTarget.h"
#include "FGTrackerInput.h"
#include "FGTrain.h"
#include "FGTrainPlayer.h"
#include "FGWorldStreamer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
    constexpr float CruiseSpeed = FG::CruiseSpeed;  // m/s at the start of a run
    constexpr float TopSpeed = 40.0f;               // and the most it ever gets to
    constexpr float StageSeconds = 28.0f;           // played at 40: too long between bosses
    constexpr float StartDistance = 58.0f;          // metres: alongside the station platform
    constexpr float AimAssistDegrees = 7.0f;        // how far off a shot may be and still hit

    const FVector2D CalibScreen[4] = { {0.22, 0.30}, {0.78, 0.30}, {0.78, 0.68}, {0.22, 0.68} };

    // Where each rider stands on the passenger car roof. Solo: the middle, as it always was. Two: the host up front
    // and to the left, the partner further back and to the right, with the host in view but out of the way.
    FTransform SeatFor(int32 Seat, bool bCoop, bool bVersus)
    {
        const float Z = AFGTrain::RoofCm + 96.0f;
        if (bVersus)
        {
            // A 1v1: the passenger car roof, seven metres apart, facing each other. (Ten read too small on a webcam.)
            return Seat == 0 ? FTransform(FRotator(0.0, 180.0, 0.0), FVector(350.0f, 0.0f, Z)) : FTransform(FVector(-350.0f, 0.0f, Z));
        }
        if (!bCoop) { return FTransform(FVector(AFGPresentation::PlayerForwardCm, 0.0f, Z)); }
        return FTransform(Seat == 0 ? FVector(300.0f, -55.0f, Z) : FVector(-200.0f, 55.0f, Z));
    }

    FString NameOf(const AFGTrainPlayer* P)
    {
        const APlayerState* PS = P ? P->GetPlayerState() : nullptr;
        return PS ? PS->GetPlayerName().ToUpper() : FString(TEXT("SOMEONE"));
    }
}

AFGIronHorseGameMode::AFGIronHorseGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = AFGTrainPlayer::StaticClass();
    HUDClass = AFGHud::StaticClass();
    GameStateClass = AFGGameState::StaticClass();
    PlayerStateClass = AFGPlayerState::StaticClass();
    PlayerControllerClass = AFGPlayerController::StaticClass();
}

AFGGameState* AFGIronHorseGameMode::GS() const
{
    return GetGameState<AFGGameState>();
}

FString AFGIronHorseGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
    const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
    if (AFGPlayerState* PS = NewPlayerController ? NewPlayerController->GetPlayerState<AFGPlayerState>() : nullptr)
    {
        // First free spot on the roof.
        bool bFrontTaken = false;
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            const AFGPlayerState* Other = It->Get() && It->Get() != NewPlayerController ? It->Get()->GetPlayerState<AFGPlayerState>() : nullptr;
            bFrontTaken |= Other && Other->Seat == 0;
        }
        PS->Seat = bFrontTaken ? 1 : 0;
        // Not the computer's network name, which is what a join sends: the seat is the name.
        ChangeName(NewPlayerController, PS->Seat == 0 ? TEXT("Player 1") : TEXT("Player 2"), false);
    }
    return Error;
}

APawn* AFGIronHorseGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform&)
{
    // Whatever the level's PlayerStart says, the player stands on the passenger car roof at the origin facing up the train.
    const AFGPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AFGPlayerState>() : nullptr;
    const FTransform Seat = SeatFor(PS ? PS->Seat : 0, GetNetMode() == NM_ListenServer, bVersus);
    APawn* Pawn = Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, Seat);
    if (AFGTrainPlayer* P = Cast<AFGTrainPlayer>(Pawn))
    {
        P->SetSeat(Seat.GetLocation(), float(Seat.Rotator().Yaw));
        if (Lap > 0) { P->SetWeapon(Lap); }     // joined mid-run: today's gun
    }
    return Pawn;
}

void AFGIronHorseGameMode::Logout(AController* Exiting)
{
    UE_LOG(LogTemp, Log, TEXT("IronHorse: %s left"), Exiting && Exiting->PlayerState ? *Exiting->PlayerState->GetPlayerName() : TEXT("someone"));
    Super::Logout(Exiting);
}

TArray<AFGTrainPlayer*> AFGIronHorseGameMode::Players() const
{
    TArray<AFGTrainPlayer*> Out;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (AFGTrainPlayer* P = It->Get() ? Cast<AFGTrainPlayer>(It->Get()->GetPawn()) : nullptr) { Out.Add(P); }
    }
    return Out;
}

TArray<AFGTrainPlayer*> AFGIronHorseGameMode::AlivePlayers() const
{
    TArray<AFGTrainPlayer*> Out = Players();
    Out.RemoveAll([](const AFGTrainPlayer* P) { return P->bDowned; });
    return Out;
}

AFGTrainPlayer* AFGIronHorseGameMode::LocalPlayer() const
{
    const APlayerController* PC = GetWorld()->GetFirstPlayerController();
    return PC ? Cast<AFGTrainPlayer>(PC->GetPawn()) : nullptr;
}

void AFGIronHorseGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    // Read before anyone is placed on the roof: a 1v1 seats the players facing each other.
    bVersus = UGameplayStatics::HasOption(Options, TEXT("versus"));
}

void AFGIronHorseGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (UFGSession* Session = UFGSession::Get(this)) { Session->Preload(); }

    Presentation = AFGPresentation::Get(this);
    World = Presentation->World;
    Train = Presentation->Train;
    BanditTrain = Presentation->BanditTrain;
    Presentation->AddTickPrerequisiteActor(this);       // the line moves here first, then gets dressed
    World->Queue({ TEXT("Flat_A"), TEXT("Landmark_Station_A"), TEXT("Flat_B"), TEXT("Rocky_A"), TEXT("Landmark_WaterTower_A"), TEXT("Cactus_A"), TEXT("CurveL_A"), TEXT("Mesa_A"), TEXT("CurveR_A") });
    World->bAllowRandomLandmarks = false;
    World->SetDistance(StartDistance);
    GS()->DefsChecksum = World->DefsChecksum();

    // Test switches: -FGAuto plays by itself, -FGGod takes no damage, -FGLap=2 starts on that lap, -FGStage=3 jumps to the boss.
    // (-FGPerf and -FGShots=4 are AFGPresentation's, so they work on a co-op client too.)
    bAutoPlay = FParse::Param(FCommandLine::Get(), TEXT("FGAuto"));
    bGod = FParse::Param(FCommandLine::Get(), TEXT("FGGod"));
    FParse::Value(FCommandLine::Get(), TEXT("FGLap="), TestLap);         // start on this lap
    FParse::Value(FCommandLine::Get(), TEXT("FGStage="), TestStage);     // 0 riders, 1 boarders, 2 other train, 3 boss
    bRideAlone = FParse::Param(FCommandLine::Get(), TEXT("FGAlone"));   // hosting, but do not wait on the title for a partner
    UE_LOG(LogTemp, Log, TEXT("IronHorse: %s"), GetNetMode() != NM_ListenServer ? TEXT("solo") : (bVersus ? TEXT("hosting co-op (1v1 duel) on UDP 7777") : TEXT("hosting co-op on UDP 7777")));
    SetPhase(EFGPhase::Title);
}

// ------------------------------------------------------------------ phases

void AFGIronHorseGameMode::SetPhase(EFGPhase NewPhase)
{
    Phase = NewPhase;
    PhaseTime = 0.0f;
    UE_LOG(LogTemp, Log, TEXT("IronHorse: phase %d, score %d"), int32(NewPhase), Score);
    switch (Phase)
    {
    case EFGPhase::Title:
        Prompt = TEXT("MAKE A FINGER GUN");
        SubPrompt = TEXT("point it at the screen");
        bCrosshairVisible = false;
        break;
    case EFGPhase::Calibrate:
        if (AFGTrainPlayer* Me = LocalPlayer()) { Me->Tracker->SendCalibBegin(); }
        CalibIndex = 0;
        bCrosshairVisible = false;      // point at the bottle naturally, do not steer a cursor onto it
        Prompt = TEXT("SHOOT THE BOTTLE");
        SubPrompt = TEXT("drop your thumb to fire");
        SpawnCalibBottle();
        break;
    case EFGPhase::Tutorial:
        bCrosshairVisible = true;
        Prompt = TEXT("SHOOT THE CANS");
        SubPrompt = TEXT("");
        SpawnCans();
        break;
    case EFGPhase::Bell:
        Prompt = TEXT("SHOOT THE BELL TO DEPART");
        SubPrompt = TEXT("");
        SpawnBell();
        break;
    case EFGPhase::Ride:
        Prompt = TEXT("");
        SubPrompt = TEXT("");
        break;
    case EFGPhase::Showdown:
        EveryoneLeave();
        World->bStraightOnly = true;
        World->bAllowRandomLandmarks = false;
        World->ClearQueue();            // no tunnel, canyon or bridge turning up in the middle of a duel
        ShowdownStep = 0;
        ShowdownTimer = 3.0f;
        Prompt = TEXT("");
        break;
    case EFGPhase::Versus:
        bCrosshairVisible = true;
        VersusStep = 0;
        VersusTimer = 3.5f;
        PendingWinner = nullptr;
        Prompt = TEXT("1 v 1");
        SubPrompt = TEXT("first to hit the other wins the round.  fire before DRAW and you lose it.  three hats each");
        break;
    case EFGPhase::Result:
        EveryoneLeave();
        ResultTime = 0.0f;
        GS()->ResultSince = GS()->Now();
        bCrosshairVisible = true;
        Prompt = TEXT("");
        SubPrompt = TEXT("");
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            if (AFGPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<AFGPlayerState>() : nullptr) { PS->bVotedRide = false; }
        }
        break;
    }
}

void AFGIronHorseGameMode::TickTitle()
{
    if (GetNetMode() == NM_Standalone)
    {
        // Solo: the main menu (AFGPlayerController) decides. Started straight into duels (?duel), or playing itself, go.
        Prompt = SubPrompt = TEXT("");
        if (bAutoPlay && PhaseTime > 1.0f) { MenuRide(); }
        return;
    }
    const TArray<AFGTrainPlayer*> All = Players();
    if (All.Num() != LastPlayerCount)
    {
        LastPlayerCount = All.Num();
        PhaseTime = 0.0f;           // someone just joined: give them a moment to see it
    }
    // Hosting: wait here for the partner, unless the host would rather ride alone (Enter).
    bWaitingForPartner = GetNetMode() == NM_ListenServer && All.Num() < 2 && (bVersus || !bRideAlone);
    if (bWaitingForPartner)
    {
        Prompt = bVersus ? TEXT("WAITING FOR YOUR OPPONENT") : TEXT("WAITING FOR A PARTNER");
        SubPrompt = TEXT("");
        return;
    }
    Prompt = TEXT("MAKE A FINGER GUN");
    SubPrompt = All.Num() > 1 ? TEXT("both of you: point it at the screen") : TEXT("point it at the screen");
    bool bReady = All.Num() > 0;
    for (const AFGTrainPlayer* P : All)
    {
        const FFGTrackerState In = P->InputState();
        bReady &= In.bAimValid && In.bGunPose;
    }
    if ((PhaseTime > 2.5f && bReady) || (bAutoPlay && PhaseTime > 1.0f))
    {
        if (bVersus) { StartVersus(); }
        else { SetPhase(EFGPhase::Tutorial); }
    }
}

void AFGIronHorseGameMode::MenuRide()
{
    if (Phase == EFGPhase::Title) { SetPhase(EFGPhase::Tutorial); }
}

void AFGIronHorseGameMode::RideAlone()
{
    if (Phase == EFGPhase::Title) { bRideAlone = true; }
}

FVector AFGIronHorseGameMode::ScreenToWorldPoint(FVector2D Screen, float DistanceCm) const
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    int32 W = 0, H = 0;
    FVector Origin, Dir;
    if (PC) { PC->GetViewportSize(W, H); }
    if (PC && W > 0 && PC->DeprojectScreenPositionToWorld(Screen.X * W, Screen.Y * H, Origin, Dir))
    {
        return Origin + Dir * DistanceCm;
    }
    const AFGTrainPlayer* Me = LocalPlayer();
    return (Me ? Me->HeadLocation() : FVector::ZeroVector) + FVector(DistanceCm, (Screen.X - 0.5f) * DistanceCm, (0.5f - Screen.Y) * DistanceCm);
}

AFGTarget* AFGIronHorseGameMode::SpawnTarget(UStaticMesh* Mesh, const FTransform& At, float Radius)
{
    AFGTarget* T = GetWorld()->SpawnActor<AFGTarget>(AFGTarget::StaticClass(), At);
    T->SetMesh(Mesh, float(At.GetScale3D().X));
    T->Radius = Radius;
    Targets.Add(T);
    return T;
}

void AFGIronHorseGameMode::SpawnCalibBottle()
{
    const FVector2D Screen = CalibScreen[CalibIndex];
    const FVector At = ScreenToWorldPoint(Screen, 750.0f);
    const float Scale = 2.6f;
    AFGTarget* Bottle = SpawnTarget(FGAssets::StaticMesh(TEXT("props"), CalibIndex % 2 ? TEXT("SM_Bottle_Brown") : TEXT("SM_Bottle_Green")), FTransform(FRotator::ZeroRotator, At - FVector(0, 0, 15.0f * Scale), FVector(Scale)), 60.0f);
    Bottle->CentreOffset = FVector(0, 0, 15.0f * Scale);
    // Something for it to stand on: a post from the roof.
    const float PostHeight = Bottle->GetActorLocation().Z - AFGTrain::RoofCm;
    if (PostHeight > 5.0f)
    {
        UStaticMeshComponent* Post = NewObject<UStaticMeshComponent>(Bottle);
        Post->SetStaticMesh(FGAssets::StaticMesh(TEXT("props"), TEXT("SM_Crate_Small")));
        Post->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Post->SetUsingAbsoluteScale(true);
        Post->SetUsingAbsoluteLocation(true);
        Post->SetupAttachment(Bottle->Mesh);
        Post->RegisterComponent();
        Post->SetWorldScale3D(FVector(0.35f, 0.35f, PostHeight / 60.0f));
        Post->SetWorldLocation(FVector(Bottle->GetActorLocation().X, Bottle->GetActorLocation().Y, AFGTrain::RoofCm));
    }
    if (AFGTrainPlayer* Me = LocalPlayer()) { Me->Tracker->SendCalibTarget(Screen.X, Screen.Y); }
    Bottle->OnShot = [this](AFGTarget*, AFGTrainPlayer* By)
    {
        AddScore(10, By);
        PlaySfx(TEXT("glass"));
        if (++CalibIndex < 4) { SpawnCalibBottle(); }
        else { SetPhase(EFGPhase::Tutorial); }
    };
}

void AFGIronHorseGameMode::SpawnCans()
{
    // Two cans each on the boxcar roof ahead: enough to learn the trigger, then the bell.
    CansLeft = 2 * FMath::Max(1, Players().Num());
    for (int32 i = 0; i < CansLeft; ++i)
    {
        const FVector OnBoxcar(-450.0f + (i % 2) * 40.0f + (i / 2) * 240.0f, ((i % 2) * 2 - 1) * 90.0f, AFGTrain::RoofCm);
        const FVector Base = (FTransform(OnBoxcar) * Presentation->AnchorTransform(FFGAnchor::OwnCar(2))).GetLocation();
        AFGTarget* Crate = SpawnTarget(FGAssets::StaticMesh(TEXT("props"), TEXT("SM_Crate")), FTransform(FRotator(0, i * 17.0f, 0), Base), 0.0f);
        Crate->bActive = false;
        Crate->Anchor = FFGAnchor::OwnCar(2);
        Crate->Local = OnBoxcar;
        const float Scale = 3.2f;
        AFGTarget* Can = SpawnTarget(FGAssets::StaticMesh(TEXT("props"), TEXT("SM_TinCan")), FTransform(FRotator::ZeroRotator, Base + FVector(0, 0, 90.0f), FVector(Scale)), 28.0f);
        Can->CentreOffset = FVector(0, 0, 5.5f * Scale);
        Can->OnShot = [this](AFGTarget*, AFGTrainPlayer* By)
        {
            AddScore(10, By);
            PlaySfx(TEXT("ricochet"));
            if (--CansLeft <= 0) { SetPhase(EFGPhase::Bell); }
        };
    }
}

void AFGIronHorseGameMode::SpawnBell()
{
    AFGTarget* Bell = SpawnTarget(nullptr, FTransform(FVector(1200.0f, 420.0f, 300.0f)), 110.0f);
    Bell->bBreaks = false;
    USkeletalMeshComponent* Rig = World ? World->FindRig(TEXT("StationBell")) : nullptr;
    if (Rig)
    {
        // As modelled the bell hangs 2.5 m up, far below a player on the roof. A taller post (AFGPresentation) puts it near eye level.
        const float Tall = 2.3f;
        GS()->bBellRaised = true;
        Bell->PlaceAt(Rig->GetComponentLocation());
        Bell->CentreOffset = FVector(0, 0, 250.0f * Tall);
        Bell->Radius = 160.0f;
    }
    else
    {
        // No station in view: hang a start sign in the middle of the screen instead.
        Bell->SetMesh(FGAssets::StaticMesh(TEXT("props"), TEXT("SM_StartSign")), 1.0f);
        Bell->PlaceAt(ScreenToWorldPoint(FVector2D(0.5, 0.45), 900.0f), FRotator(0, 90.0f, 0));
        Bell->CentreOffset = FVector(0, 0, 120.0f);
    }
    const bool bRig = Rig != nullptr;
    Bell->OnShot = [this, bRig](AFGTarget* T, AFGTrainPlayer*)
    {
        if (bRig) { GS()->MulticastRingBell(); }
        T->bActive = false;
        T->SetLifeSpan(bRig ? 0.1f : 1.0f);
        Depart();
    };
}

void AFGIronHorseGameMode::Depart()
{
    PlaySfx(TEXT("bell"));
    PlaySfx(TEXT("whistle"));
    for (AFGTarget* T : Targets) { if (T && T->bActive == false && T->Radius == 0.0f) { T->SetLifeSpan(6.0f); } }
    RideTime = 0.0f;
    DistanceM = 0.0;
    World->bAllowRandomLandmarks = true;        // water towers and signal gantries turn up by themselves from here on
    BeginLap(TestLap);
    if (TestStage >= 0)
    {
        TrainSpeed = TargetSpeed();
        if (TestStage >= 3) { SetPhase(EFGPhase::Showdown); }
        else { NextStage(EFGStage(TestStage)); SpawnTimer = 2.0f; }
    }
}

// ------------------------------------------------------------------ the endless run

// Played at the first settings: "too extreme, too much going on at once". So fewer people on screen, one shooter at a
// time to begin with, longer gaps between their shots and between spawns, and a gentler climb.
float AFGIronHorseGameMode::CoopScale() const { return Players().Num() > 1 ? 1.0f : 0.0f; }
float AFGIronHorseGameMode::TargetSpeed() const { return FMath::Min(CruiseSpeed + 1.5f * Lap + RideTime / 60.0f * 1.5f, TopSpeed); }
int32 AFGIronHorseGameMode::MaxAlive() const { return FMath::Min(FMath::Clamp(2 + (Lap + 1) / 2, 2, 4) + int32(CoopScale()), 5); }
int32 AFGIronHorseGameMode::TokenLimit() const { return Lap >= 1 ? 2 : 1; }      // per player
float AFGIronHorseGameMode::ShotFlight() const { return FMath::Max(0.5f, 0.75f - 0.04f * Lap); }
float AFGIronHorseGameMode::FireDelayScale() const { return FMath::Max(0.7f, 1.3f - 0.1f * Lap); }
float AFGIronHorseGameMode::SpawnEvery() const { return FMath::Max(2.0f, 3.6f - 0.3f * Lap) * (1.0f - 0.15f * CoopScale()); }

void AFGIronHorseGameMode::BeginLap(int32 NewLap)
{
    Lap = NewLap;
    Boss = nullptr;
    Phase = EFGPhase::Ride;
    PhaseTime = 0.0f;
    Prompt = SubPrompt = TEXT("");
    World->bStraightOnly = false;
    World->bAllowRandomLandmarks = true;
    NextStage(EFGStage::Riders);
    SpawnTimer = Lap == 0 ? 8.0f : 3.0f;
    for (AFGTrainPlayer* P : Players())
    {
        if (P->WeaponIndex != Lap % 4 || Lap > 0) { P->SetWeapon(Lap); P->ClientNewGun(Lap); }     // a new gun every lap
        if (P->bDowned && Lap > 0) { Revive(P, 1); }       // beating the boss brings everyone back
    }
    if (Lap > 0)
    {
        // The first boss falls at sunset. After him it is night, and after the next one morning, and so on.
        NightTarget = Lap % 2 ? 1.0f : 0.0f;
        if (NightTarget == 0.0f) { Dusk = 0.1f; }
        const TArray<AFGTrainPlayer*> All = Players();
        ShowBanner(FString::Printf(TEXT("%s    %s"), NightTarget > 0.5f ? TEXT("NIGHT FALLS") : TEXT("DAWN"), All.Num() ? All[0]->Weapon().Name : TEXT("")), 3.5f);
    }
}

void AFGIronHorseGameMode::NextStage(EFGStage NewStage)
{
    Stage = NewStage;
    StageTime = 0.0f;
    StageFlags = 0;
    TrainTime = 0.0f;
    CrewSpawned = 0;
    bBanditTrainCrewed = false;
    UE_LOG(LogTemp, Log, TEXT("IronHorse: lap %d stage %d at %.0f s, %.0f m/s"), Lap, int32(Stage), RideTime, TrainSpeed);
}

void AFGIronHorseGameMode::SpawnBarrel(const FVector& Local, const FFGAnchor& Anchor)
{
    // Oil barrels: shoot one and everybody standing near it goes with it.
    int32 Live = 0;
    for (const AFGTarget* T : Targets) { Live += T && T->bExplosive && T->bActive; }
    if (Live >= 4) { return; }
    const float Scale = 1.25f;
    AFGTarget* Barrel = SpawnTarget(FGAssets::StaticMesh(TEXT("props"), TEXT("SM_Barrel")), FTransform(FRotator::ZeroRotator, (FTransform(Local) * Presentation->AnchorTransform(Anchor)).GetLocation(), FVector(Scale)), 65.0f);
    Barrel->bExplosive = true;
    Barrel->CentreOffset = FVector(0, 0, 45.0f * Scale);
    Barrel->Anchor = Anchor;
    Barrel->Local = Local;
    Barrel->OnShot = [this](AFGTarget* T, AFGTrainPlayer* By)
    {
        const FVector At = T->Centre();
        PlaySfx(TEXT("boom"));
        GS()->MulticastFx(EFGFxKind::Boom, At, TrainSpeed);
        AddScore(25, By);
        const TArray<TObjectPtr<AFGBandit>> Near = Bandits;        // killing edits the list
        for (AFGBandit* B : Near)
        {
            if (B && !B->IsDead() && B != Boss && FVector::Dist(B->GetActorLocation(), At) < 520.0f)
            {
                FHitResult Blast;
                Blast.ImpactPoint = Blast.Location = At;
                B->LastHitBy = By;
                UGameplayStatics::ApplyPointDamage(B, 1000.0f, (B->GetActorLocation() - At).GetSafeNormal(), Blast, By ? By->GetController() : nullptr, By, nullptr);
                AddScore(50, By);
            }
        }
    };
}

// ------------------------------------------------------------------ tick

void AFGIronHorseGameMode::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!World) { return; }
    PhaseTime += DeltaTime;
    Bandits.RemoveAll([](const AFGBandit* B) { return !IsValid(B); });
    Targets.RemoveAll([](const AFGTarget* T) { return !IsValid(T); });

    // The train
    const bool bMoving = Phase == EFGPhase::Ride || Phase == EFGPhase::Showdown || Phase == EFGPhase::Versus || (Phase == EFGPhase::Result && TrainSpeed > 0.0f);
    const float WantSpeed = !bMoving ? 0.0f : (Phase == EFGPhase::Result || Phase == EFGPhase::Versus ? CruiseSpeed * 0.6f : TargetSpeed());
    TrainSpeed = FMath::FInterpConstantTo(TrainSpeed, WantSpeed, DeltaTime, 2.6f);
    World->SetDistance(World->GetDistance() + TrainSpeed * DeltaTime);
    if (Phase == EFGPhase::Ride || Phase == EFGPhase::Showdown) { DistanceM += TrainSpeed * DeltaTime; }

    switch (Phase)
    {
    case EFGPhase::Title:
        TickTitle();
        break;
    case EFGPhase::Calibrate:
    case EFGPhase::Tutorial:
    case EFGPhase::Bell:
        break;
    case EFGPhase::Ride:
        TickRide(DeltaTime);
        break;
    case EFGPhase::Showdown:
        TickShowdown(DeltaTime);
        break;
    case EFGPhase::Versus:
        TickVersus(DeltaTime);
        break;
    case EFGPhase::Result:
        ResultTime += DeltaTime;
        if (ResultTime > 30.0f) { RideAgain(); }
        break;
    }
    // Nobody left standing: a partner who was carrying the one who is down has left, say.
    if ((Phase == EFGPhase::Ride || Phase == EFGPhase::Showdown) && Players().Num() && !AlivePlayers().Num())
    {
        bTeamDown = true;
        SetPhase(EFGPhase::Result);
    }

    TickShots(DeltaTime);
    TickHazards();
    TickLeanObstacles(DeltaTime);
    TickSky(DeltaTime);
    PublishState();
}

void AFGIronHorseGameMode::PublishState()
{
    AFGGameState* G = GS();
    if (!G) { return; }
    G->Phase = Phase;
    G->ShowdownStep = uint8(ShowdownStep);
    G->DrawCalledAt = DrawCalledAt;
    G->Lap = Lap;
    G->BossesBeaten = BossesBeaten;
    G->Score = Score;
    G->Kills = Kills;
    G->Headshots = Headshots;
    G->Dodges = Dodges;
    G->BestDrawMs = DrawTimeMs < 0.0f ? -1 : int32(DrawTimeMs);
    G->DistanceM = DistanceM;
    G->Clock.S = World->GetDistance();
    G->Clock.Speed = TrainSpeed;
    G->Clock.At = G->GetServerWorldTimeSeconds();
    G->Prompt = Prompt;
    G->SubPrompt = SubPrompt;
    G->bCrosshairVisible = bCrosshairVisible;
    G->bStayDown = bStayDown;
    G->bDuckWarning = bDuckWarning;
    G->LeanWarning = int8(LeanWarning);
    G->bTeamDown = bTeamDown;
    if (Phase != EFGPhase::Result) { G->Riders = FMath::Max(1, Players().Num()); }
    G->bWaitingForPartner = Phase == EFGPhase::Title && bWaitingForPartner;
    G->bVersus = bVersus;
    if (Phase == EFGPhase::Versus) { G->ShowdownStep = VersusStep == 3 ? 4 : (VersusStep == 2 ? 3 : 0); }     // DRAW! reads the same as the boss duel's
    G->Dusk = Dusk;
    G->NightTarget = NightTarget;
    G->SunChainYaw = SunChainYaw;
    G->bBanditTrainVisible = !BanditTrain->IsHidden();
    G->BanditTrainOffset = float(BanditTrain->Offset);
}

void AFGIronHorseGameMode::ShowBanner(const FString& Text, float Seconds)
{
    GS()->Banner = Text;
    GS()->BannerUntil = GS()->Now() + Seconds;
}

void AFGIronHorseGameMode::AddScore(int32 Points, AFGTrainPlayer* By)
{
    Score += Points;
    if (AFGPlayerState* PS = By ? By->FGPlayerState() : nullptr) { PS->Points += Points; }
}

void AFGIronHorseGameMode::AddDodge(AFGTrainPlayer* P, int32 Points)
{
    ++Dodges;
    if (AFGPlayerState* PS = P ? P->FGPlayerState() : nullptr) { ++PS->Dodges; }
    AddScore(Points, P);
}

int32 AFGIronHorseGameMode::AliveBandits() const
{
    int32 N = 0;
    for (const AFGBandit* B : Bandits) { if (B && !B->IsDead() && B->State != EFGBanditState::Leaving) { ++N; } }
    return N;
}

void AFGIronHorseGameMode::EveryoneLeave()
{
    for (AFGBandit* B : Bandits) { if (B && B != Boss) { B->Leave(); } }
}

AFGBandit* AFGIronHorseGameMode::SpawnBandit(const FFGBanditSpec& Spec)
{
    AFGBandit* B = GetWorld()->SpawnActor<AFGBandit>(AFGBandit::StaticClass(), FTransform(FVector(0, 0, -5000)));
    B->Init(Spec, this);
    Bandits.Add(B);
    ++SpawnCount;
    return B;
}

void AFGIronHorseGameMode::TickRide(float DeltaTime)
{
    RideTime += DeltaTime;
    StageTime += DeltaTime;
    SpawnTimer -= DeltaTime;
    const bool bNarrow = World->MetresTo(TEXT("narrow")) == 0.0f || World->MetresTo(TEXT("dark")) == 0.0f || World->MetresTo(TEXT("trestle")) == 0.0f;
    auto Once = [this](int32 Bit, float At) { if (StageTime < At || (StageFlags & (1 << Bit))) { return false; } StageFlags |= 1 << Bit; return true; };

    // Tunnels are a ducking section, nothing else: nobody new shows up from 150 m out, and whoever is still
    // around clears off at the mouth. A bandit cannot ride or climb aboard inside a tunnel anyway.
    const float ToDark = World->MetresTo(TEXT("dark"));
    const bool bTunnelNear = ToDark >= 0.0f && ToDark < 150.0f;
    if (ToDark == 0.0f && !bInTunnel) { EveryoneLeave(); }
    bInTunnel = ToDark == 0.0f;
    if (bTunnelNear) { SpawnTimer = FMath::Max(SpawnTimer, 1.5f); }

    // Horses cannot cross a trestle or squeeze into a slot canyon. Riders rein in well before the edge and nobody
    // new rides up until it is behind us.
    float ToGap = -1.0f;
    for (const TCHAR* Kind : { TEXT("trestle"), TEXT("narrow") })
    {
        const float D = World->MetresTo(Kind);
        if (D >= 0.0f && (ToGap < 0.0f || D < ToGap)) { ToGap = D; }
    }
    const bool bGapNear = ToGap >= 0.0f && ToGap < 220.0f;
    if (ToGap >= 0.0f && ToGap < 120.0f)
    {
        for (AFGBandit* B : Bandits) { if (B && B->IsRider() && !B->IsDead()) { B->Leave(); } }
    }

    // Never a dead stretch: if there has been nobody to shoot at for a few seconds, whatever the stage is waiting
    // for (the other train to arrive, a crew used up), a rider comes up. On the right only while the left is a railway.
    QuietTime = AliveBandits() > 0 ? 0.0f : QuietTime + DeltaTime;
    if (QuietTime > 4.0f && !bNarrow && !bTunnelNear && !bGapNear && !(Stage == EFGStage::Riders && StageTime < 7.0f && Lap == 0))
    {
        QuietTime = 0.0f;
        const bool bLeftBusy = World->MetresTo(TEXT("side_track")) >= 0.0f && World->MetresTo(TEXT("side_track")) < 300.0f;
        FFGBanditSpec Spec;
        Spec.Kind = EFGBanditKind::Rider;
        const float Side = bLeftBusy || SpawnCount % 2 == 0 ? 1.0f : -1.0f;
        Spec.Slot = FVector(FMath::FRandRange(2400.0f, 3800.0f), Side * FMath::FRandRange(620.0f, 880.0f), 0.0f);
        Spec.Scale = 1.35f;
        Spec.HorseCoat = SpawnCount;
        Spec.FirstShotDelay = FMath::FRandRange(1.5f, 2.5f) * FireDelayScale();
        SpawnBandit(Spec);
    }

    // Set pieces are queued, not placed: the streamer lays them as soon as the joint rule allows, 450 m ahead,
    // so each one turns up 10-17 s after it is asked for, in the order asked.
    switch (Stage)
    {
    case EFGStage::Riders:
    {
        if (Once(1, 0.0f) && (Lap > 0 || StageTime > 0.0f))
        {
            // Through a town without stopping: a street either side, the station in the middle.
            // (Not the station on the first lap: we have only just left one.)
            World->Queue(Lap == 0 ? TArray<FString>{ TEXT("Flat_A+town"), TEXT("Flat_B+town") } : TArray<FString>{ TEXT("Flat_A+town"), TEXT("Landmark_Station_A"), TEXT("Flat_B+town") });
        }
        if (Once(0, 6.0f))
        {
            // A trestle bridge over a gulch. Riders drop back for it: there is nowhere to ride.
            World->Queue(Lap % 2 ? TArray<FString>{ TEXT("Gulch_A"), TEXT("Flat_B"), TEXT("Gulch_CurveR_A") } : TArray<FString>{ TEXT("Gulch_A") });
        }
        if (SpawnTimer <= 0.0f && AliveBandits() < MaxAlive() && !bNarrow && !bTunnelNear && !bGapNear)
        {
            SpawnTimer = StageTime < 8.0f && Lap == 0 ? 4.0f : SpawnEvery();
            FFGBanditSpec Spec;
            Spec.Kind = EFGBanditKind::Rider;
            const float Side = SpawnCount % 2 ? -1.0f : 1.0f;
            Spec.Slot = FVector(FMath::FRandRange(2400.0f, 3800.0f), Side * FMath::FRandRange(620.0f, 880.0f), 0.0f);
            Spec.Scale = 1.35f;        // out to the side they read small. Bigger and closer.
            Spec.Mesh = SpawnCount % 5 == 4 ? TEXT("SK_Gunslinger") : (SpawnCount % 3 == 2 ? TEXT("SK_Deputy") : TEXT("SK_Bandit"));
            Spec.HorseCoat = SpawnCount;
            Spec.FirstShotDelay = FMath::FRandRange(1.5f, 3.0f) * FireDelayScale();
            SpawnBandit(Spec);
        }
        if (StageTime > StageSeconds) { EveryoneLeave(); NextStage(EFGStage::Boarders); SpawnTimer = 2.0f; }
        break;
    }
    case EFGStage::Boarders:
    {
        if (Once(0, 0.0f))
        {
            World->Queue({ TEXT("CanyonDeep_Entry_A"), TEXT("CanyonDeep_Mid_A"), TEXT("CanyonDeep_CurveL_A"), TEXT("CanyonDeep_Mid_A"), TEXT("CanyonDeep_CurveR_A"), TEXT("CanyonDeep_Exit_A"), TEXT("Flat_A") });
        }
        if (Once(1, 6.0f))
        {
            // Straight tunnel pieces only: those are the ones the streamer widens.
            World->Queue({ TEXT("Tunnel_Entry_A"), TEXT("Tunnel_Mid_A"), TEXT("Tunnel_Mid_A"), TEXT("Tunnel_Mid_A"), TEXT("Tunnel_Exit_A"), TEXT("Rocky_A") });
        }
        if (Once(2, StageSeconds - 450.0f / FMath::Max(TrainSpeed, 10.0f) - 8.0f))
        {
            TArray<FString> Line = { TEXT("SideTrack_Start_A") };
            const TCHAR* Pattern[] = { TEXT("SideTrack_Mid_A"), TEXT("SideTrack_Mid_A"), TEXT("SideTrack_CurveL_A"), TEXT("SideTrack_Mid_A"), TEXT("SideTrack_CurveR_A"), TEXT("SideTrack_Mid_A") };
            const int32 Pieces = FMath::CeilToInt32((StageSeconds + 22.0f) * TargetSpeed() / 50.0f);
            for (int32 i = 0; i < Pieces; ++i) { Line.Add(Pattern[i % 6]); }
            Line.Add(TEXT("SideTrack_End_A"));
            World->Queue(Line);
        }
        if (SpawnTimer <= 0.0f && AliveBandits() < MaxAlive() && !bTunnelNear)
        {
            SpawnTimer = SpawnEvery();
            FFGBanditSpec Spec;
            // In the boxcar's own frame (its centre is 13.9 m up the train).
            static const FVector Slots[] = { {10, -45, 0}, {260, 50, 0}, {510, -35, 0}, {110, 55, 0}, {410, -50, 0} };
            Spec.Slot = Slots[SpawnCount % 5] + FVector(0, 0, AFGTrain::RoofCm);
            Spec.Anchor = FFGAnchor::OwnCar(2);
            // From the second lap one in four boarders brings dynamite.
            const bool bDynamite = Lap >= 2 && SpawnCount % 5 == 1;
            Spec.Kind = bDynamite ? EFGBanditKind::Dynamiter : EFGBanditKind::Boarder;
            Spec.Mesh = bDynamite ? TEXT("SK_Dynamiter") : (SpawnCount % 4 == 3 ? TEXT("SK_Heavy") : (SpawnCount % 2 ? TEXT("SK_Bandit") : TEXT("SK_Deputy")));
            Spec.Health = Spec.Mesh == TEXT("SK_Heavy") ? 50.0f : 25.0f;
            Spec.FirstShotDelay = FMath::FRandRange(1.0f, 2.2f) * FireDelayScale();
            if (SpawnCount % 3 == 0) { SpawnBarrel(Spec.Slot + FVector(-60.0f, Spec.Slot.Y > 0 ? -95.0f : 95.0f, 0.0f), FFGAnchor::OwnCar(2)); }
            SpawnBandit(Spec);
        }
        if (StageTime > StageSeconds) { EveryoneLeave(); NextStage(EFGStage::SecondTrain); }
        break;
    }
    case EFGStage::SecondTrain:
    {
        // The bandit train pulls alongside once there is a second line to run on, and drops back before it ends.
        const float ToSide = World->MetresTo(TEXT("side_track"));
        const bool bTimeUp = TrainTime > StageSeconds + 8.0f || (TrainTime > 8.0f && ToSide != 0.0f) || (BanditTrain->IsHidden() && StageTime > 35.0f);
        if (BanditTrain->IsHidden() && ToSide == 0.0f && !bTimeUp)
        {
            BanditTrain->SetActorHiddenInGame(false);
            BanditTrain->Offset = -260.0;
            PlaySfx(TEXT("whistle"), 0.7f, 0.8f);
        }
        if (!BanditTrain->IsHidden() && !bTimeUp)
        {
            TrainTime += DeltaTime;
            BanditTrain->Offset = FMath::FInterpTo(BanditTrain->Offset, 24.0, DeltaTime, 0.55f);
            if (!bBanditTrainCrewed && BanditTrain->Offset > -10.0)
            {
                bBanditTrainCrewed = true;
                SpawnTimer = 0.0f;
                SpawnBarrel(FVector(-260.0f, 0.0f, AFGTrain::RoofCm), FFGAnchor::BanditCar(3));
                SpawnBarrel(FVector(200.0f, 0.0f, AFGTrain::RoofCm), FFGAnchor::BanditCar(2));
            }
            // A crew, not a fountain: so many per visit, and they climb up the far side.
            const int32 Crew = int32((6 + 2 * Lap) * (1.0f + 0.5f * CoopScale()));
            if (bBanditTrainCrewed && SpawnTimer <= 0.0f && AliveBandits() < MaxAlive() && CrewSpawned < Crew)
            {
                SpawnTimer = SpawnEvery();
                struct FSeat { int32 Car; FVector Local; };
                static const FSeat Seats[] = { {3, {350, 0, 0}}, {2, {-150, 0, 0}}, {3, {-400, 30, 0}}, {2, {300, -20, 0}}, {4, {0, 0, -130}}, {3, {0, -30, 0}} };
                const FSeat& Seat = Seats[SpawnCount % 6];
                FFGBanditSpec Spec;
                Spec.Kind = SpawnCount % 6 == 2 ? EFGBanditKind::Dynamiter : EFGBanditKind::TrainShooter;
                Spec.Mesh = Spec.Kind == EFGBanditKind::Dynamiter ? TEXT("SK_Dynamiter") : TEXT("SK_Rifleman");
                Spec.Slot = Seat.Local + FVector(0, 0, AFGTrain::RoofCm);
                Spec.Anchor = FFGAnchor::BanditCar(Seat.Car);
                Spec.FirstShotDelay = FMath::FRandRange(1.2f, 2.5f) * FireDelayScale();
                SpawnBandit(Spec);
                ++CrewSpawned;
            }
        }
        if (bTimeUp)
        {
            if (StageFlags == 0) { StageFlags = 1; EveryoneLeave(); }
            // It loses ground slowly, a few metres a second, and slips out of sight behind us. It used to shoot backwards
            // at 180 m/s, which read as vanishing. The duel starts once it is behind the camera; it is hidden later.
            BanditTrain->Offset = FMath::FInterpConstantTo(BanditTrain->Offset, -200.0, DeltaTime, 7.0f + TrainSpeed * 0.12f);
            if (BanditTrain->IsHidden() || BanditTrain->Offset < -45.0)
            {
                for (AFGTarget* T : Targets) { if (T && T->bExplosive && T->Local.Y == 0.0f) { T->Destroy(); } }      // the ones on the other train
                SetPhase(EFGPhase::Showdown);
            }
        }
        break;
    }
    }
}

float AFGIronHorseGameMode::DrawGrace() const
{
    // A partner across the internet sees DRAW! half a round trip late. Give the boss that much longer, up to 0.15 s.
    float Grace = 0.0f;
    for (const AFGTrainPlayer* P : AlivePlayers())
    {
        const APlayerState* PS = P->GetPlayerState();
        if (!P->IsLocallyControlled() && PS) { Grace = FMath::Max(Grace, FMath::Min(0.15f, PS->GetPingInMilliseconds() / 2000.0f)); }
    }
    return Grace;
}

void AFGIronHorseGameMode::TickShowdown(float DeltaTime)
{
    if (!BanditTrain->IsHidden())
    {
        BanditTrain->Offset = FMath::FInterpConstantTo(BanditTrain->Offset, -200.0, DeltaTime, 7.0f + TrainSpeed * 0.12f);
        if (BanditTrain->Offset < -110.0) { BanditTrain->SetActorHiddenInGame(true); }
    }
    ShowdownTimer -= DeltaTime;
    const double Now = GetWorld()->GetTimeSeconds();
    switch (ShowdownStep)
    {
    case 0:     // quiet, then he lands. Not until everything already laid ahead (450 m) is plain open track.
        if (ShowdownTimer <= 0.0f && !World->EventsWithin(500.0f))
        {
            FFGBanditSpec Spec;
            Spec.Kind = EFGBanditKind::Boss;
            Spec.Mesh = TEXT("SK_Boss");
            Spec.Slot = FVector(-90.0f, 0.0f, AFGTrain::RoofCm);
            Spec.Anchor = FFGAnchor::OwnCar(2);
            Boss = SpawnBandit(Spec);
            ShowdownStep = 1;
        }
        break;
    case 1:     // waiting for OnBossLanded
        break;
    case 2:     // holster: every rider still standing (the HUD tells each how, tracker or keys)
    {
        Prompt = TEXT("HOLSTER");
        SubPrompt = TEXT("");
        bool bAllHolstered = true;
        for (const AFGTrainPlayer* P : AlivePlayers()) { bAllHolstered &= P->InputState().bHolstered; }
        HolsteredFor = bAllHolstered ? HolsteredFor + DeltaTime : 0.0f;
        HolsterWait += DeltaTime;
        // A booth player who never finds the holster pose still gets their duel.
        if (HolsteredFor > 0.6f || HolsterWait > 6.0f)
        {
            Prompt = TEXT("WAIT FOR IT...");
            ShowdownTimer = FMath::FRandRange(1.8f, 3.4f);
            ShowdownStep = 3;
            PlaySfx(TEXT("heartbeat"));
        }
        break;
    }
    case 3:     // the wait. Only a SHOT before the whistle is too early (see ResolvePlayerShot). This used to watch the
                // holster flag as well, and that flag flickers whenever the body model loses the arm for a frame:
                // players were told TOO EARLY while standing perfectly still.
        if (ShowdownTimer <= 0.0f)
        {
            Prompt = TEXT("DRAW!");
            PlaySfx(TEXT("whistle"));
            DrawCalledAt = Now;
            if (Boss) { Boss->Draw(FMath::Max(0.6f, 1.15f - 0.15f * Lap) + DrawGrace()); }
            ShowdownStep = 4;
        }
        break;
    case 4:     // live. Resolved by OnBanditKilled, or by his bullets.
        if (PhaseTime > 0.0f && Now - DrawCalledAt > 2.0) { Prompt = TEXT(""); }
        if (Boss && !Boss->IsDead() && Now - DrawCalledAt > 3.0 && ShowdownTimer <= 0.0f)
        {
            ShowdownTimer = 2.2f;
            Boss->Draw(0.7f);           // he keeps shooting until someone drops
        }
        break;
    case 5:     // after a shot too early: straight back to the wait, no need to holster again
        if (ShowdownTimer <= 0.0f)
        {
            Prompt = TEXT("WAIT FOR IT...");
            ShowdownTimer = FMath::FRandRange(1.8f, 3.2f);
            ShowdownStep = 3;
        }
        break;
    case 6:     // aftermath
        if (ShowdownTimer <= 0.0f) { BeginLap(Lap + 1); }        // no ending: it goes round again, harder, until you drop
        break;
    }
}

void AFGIronHorseGameMode::OnBossLanded()
{
    ShowdownStep = 2;
    HolsteredFor = 0.0f;
    HolsterWait = 0.0f;
    PlaySfx(TEXT("thud"));
}

// ------------------------------------------------------------------ shooting

bool AFGIronHorseGameMode::PlayerMayFire(const AFGTrainPlayer* P) const
{
    if (Phase == EFGPhase::Versus) { return P && (VersusStep == 2 || VersusStep == 3); }      // early shots count: as fouls
    return P && !P->bDowned && Phase != EFGPhase::Title && Phase != EFGPhase::Result;
}

void AFGIronHorseGameMode::VoteRideAgain(APlayerController* PC)
{
    if (Phase != EFGPhase::Result || ResultTime < 1.0f) { return; }
    if (AFGPlayerState* PS = PC ? PC->GetPlayerState<AFGPlayerState>() : nullptr) { PS->bVotedRide = true; }
    // Everyone has to be ready, or the one still reading the poster is thrown back to the station.
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const AFGPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<AFGPlayerState>() : nullptr;
        if (PS && !PS->bVotedRide) { return; }
    }
    RideAgain();
}

void AFGIronHorseGameMode::Revive(AFGTrainPlayer* P, int32 WithHats)
{
    if (P->bDowned) { UE_LOG(LogTemp, Log, TEXT("IronHorse: %s is back"), *NameOf(P)); }
    P->bDowned = false;
    P->bGunHidden = false;
    P->SetHats(WithHats);
}

void AFGIronHorseGameMode::RideAgain()
{
    if (bVersus) { StartVersus(); return; }
    UE_LOG(LogTemp, Log, TEXT("IronHorse: ride again"));
    for (AFGBandit* B : Bandits) { if (B) { B->Destroy(); } }
    for (AFGTarget* T : Targets) { if (T) { T->Destroy(); } }
    Bandits.Reset();
    Targets.Reset();
    Shots.Reset();
    Boss = nullptr;

    Score = Kills = Headshots = Dodges = 0;
    Lap = BossesBeaten = SpawnCount = CrewSpawned = 0;
    DrawTimeMs = -1.0f;
    DistanceM = 0.0;
    RideTime = ResultTime = TrainSpeed = QuietTime = 0.0f;
    bTeamDown = bBossBeaten = bInTunnel = bBanditTrainCrewed = bStayDown = bDuckWarning = false;
    LeanWarning = 0.0f;
    ObstacleTimer = 12.0f;
    ShowdownStep = 0;
    Dusk = NightTarget = 0.0f;
    SunChainYaw = 150.0f;

    for (AFGTrainPlayer* P : Players())
    {
        Revive(P, 3);
        P->TokensIn = 0;
        P->InvulnerableFor = 0.0f;
        P->LastHazardS = -1.0;
        P->SetWeapon(0);
        P->ClientNewGun(0);
        if (AFGPlayerState* PS = P->FGPlayerState()) { PS->ResetTally(); }
    }
    GS()->bBellRaised = false;
    GS()->BannerUntil = 0.0;

    BanditTrain->SetActorHiddenInGame(true);
    BanditTrain->Offset = -400.0;
    ++GS()->LineEpoch;                  // co-op clients tear their copy of the line up too
    World->ResetLine();
    World->bAllowRandomLandmarks = false;
    World->Queue({ TEXT("Flat_A"), TEXT("Landmark_Station_A"), TEXT("Flat_B"), TEXT("Rocky_A"), TEXT("Landmark_WaterTower_A"), TEXT("Cactus_A"), TEXT("CurveL_A"), TEXT("Mesa_A"), TEXT("CurveR_A") });
    World->SetDistance(StartDistance);
    Train->Place(World, 0.0f);

    SetPhase(EFGPhase::Tutorial);       // straight to the cans: they have seen the title
}

bool AFGIronHorseGameMode::ResolvePlayerShot(AFGTrainPlayer* P, const FVector& Origin, const FVector& Dir, const FVector& Muzzle)
{
    if (Phase == EFGPhase::Versus) { return ResolveVersusShot(P, Origin, Dir, Muzzle); }
    const FFGWeapon& Gun = P->Weapon();
    // Webcam aim is noisy, so be generous: anything within a few degrees of the ray counts. Nearest to the ray wins.
    const float Assist = (Phase == EFGPhase::Bell ? 6.0f : AimAssistDegrees) * Gun.AssistScale;
    const bool bShowdownLocked = Phase == EFGPhase::Showdown && ShowdownStep != 4;     // he can only be shot once DRAW is called
    const FGCombat::FShotHit Hit = FGCombat::FindHit(GetWorld(), Origin, Dir, Assist, bShowdownLocked, Phase == EFGPhase::Calibrate);

    if (Phase == EFGPhase::Showdown && ShowdownStep == 3)
    {
        Prompt = TEXT("TOO EARLY");
        ShowdownTimer = 1.2f;
        ShowdownStep = 5;
        if (Players().Num() > 1) { ShowBanner(FString::Printf(TEXT("%s JUMPED THE GUN"), *NameOf(P)), 1.5f); }
    }

    // Misses kick up dust where the ray meets the ground. Everyone else sees the shot leave the shooter's gun.
    if (!Hit.Any() && Hit.bGround) { GS()->MulticastFx(EFGFxKind::Dust, Hit.Point, TrainSpeed); }
    GS()->MulticastPlayerShot(P, Muzzle, Hit.Point, uint8(Gun.Tracers), Gun.SfxPitch);

    if (Hit.Bandit)
    {
        Hit.Bandit->bHeadshot = Hit.bHead;
        Hit.Bandit->LastHitBy = P;
        FHitResult Fake;
        Fake.ImpactPoint = Fake.Location = Hit.Point;
        UGameplayStatics::ApplyPointDamage(Hit.Bandit, Hit.bHead ? 100.0f : P->ShotDamage, Dir, Fake, P->GetController(), P, nullptr);
        if (!Hit.Bandit->IsDead()) { Hit.Bandit->Play(TEXT("hit")); PlaySfx(TEXT("grunt")); }
        int32 Extra = Gun.MaxHits - 1;
        for (const TPair<float, AFGBandit*>& Other : Hit.AlsoHit)
        {
            if (Extra <= 0) { break; }
            if (Other.Value == Hit.Bandit || Other.Value->IsDead()) { continue; }
            FVector Chest, Head;
            Other.Value->AimPoints(Chest, Head);
            Fake.ImpactPoint = Fake.Location = Chest;
            Other.Value->bHeadshot = false;
            Other.Value->LastHitBy = P;
            UGameplayStatics::ApplyPointDamage(Other.Value, P->ShotDamage, Dir, Fake, P->GetController(), P, nullptr);
            --Extra;
        }
        GS()->MulticastFx(EFGFxKind::HitDust, Hit.Point, 0.0f);
    }
    else if (Hit.Target)
    {
        Hit.Target->Shot(P);
    }
    return Hit.Any();
}

bool AFGIronHorseGameMode::CanSee(const AFGTrainPlayer* P, const FVector& WorldPoint) const
{
    return FGCombat::CanSee(P->FirstPersonCamera->GetComponentTransform(), P->FirstPersonCamera->FieldOfView, P->ViewAspect, WorldPoint);
}

AFGTrainPlayer* AFGIronHorseGameMode::PickTarget(const FVector& WorldPoint)
{
    // Never from behind or off screen: a shot the player could not see is not dodgeable. Of those who can see it,
    // whoever has the fewest guns on them already, so neither rider is left out or ganged up on.
    if (Phase == EFGPhase::Result || bTeamDown) { return nullptr; }
    AFGTrainPlayer* Best = nullptr;
    for (AFGTrainPlayer* P : AlivePlayers())
    {
        if (P->TokensIn >= TokenLimit() || !CanSee(P, WorldPoint)) { continue; }
        if (!Best || P->TokensIn < Best->TokensIn || (P->TokensIn == Best->TokensIn && FMath::RandBool())) { Best = P; }
    }
    if (Best) { ++Best->TokensIn; }
    return Best;
}

void AFGIronHorseGameMode::ReleaseAttackToken(AFGTrainPlayer* P)
{
    if (P) { P->TokensIn = FMath::Max(0, P->TokensIn - 1); }
}

void AFGIronHorseGameMode::OnTelegraph(AFGBandit* Bandit)
{
    // The bandit flashes red at the barrel for the 0.7 s (AFGBandit::Warning). Here: the rising tone.
    PlaySfx(TEXT("telegraph"), 0.8f);
}

void AFGIronHorseGameMode::OnBanditKilled(AFGBandit* Bandit)
{
    AFGTrainPlayer* By = Bandit->LastHitBy.Get();
    AFGPlayerState* PS = By ? By->FGPlayerState() : nullptr;
    ++Kills;
    if (PS) { ++PS->Kills; }
    AddScore(100, By);
    if (Bandit->bHeadshot)
    {
        ++Headshots;
        if (PS) { ++PS->Headshots; }
        AddScore(50, By);
        // A headshot wins a hat back, for whoever on the roof needs it most. A partner who is down counts as none,
        // and comes back with it.
        AFGTrainPlayer* Neediest = nullptr;
        int32 Least = 3;
        for (AFGTrainPlayer* P : Players())
        {
            const int32 H = P->bDowned ? 0 : P->Hats;
            if (H < Least) { Least = H; Neediest = P; }
        }
        if (Neediest)
        {
            if (Neediest->bDowned)
            {
                Revive(Neediest, 1);
                ShowBanner(FString::Printf(TEXT("%s IS BACK"), *NameOf(Neediest)), 1.8f);
            }
            else
            {
                Neediest->SetHats(Neediest->Hats + 1);
                ShowBanner(Players().Num() > 1 ? FString::Printf(TEXT("+1 HAT  %s"), *NameOf(Neediest)) : FString(TEXT("+1 HAT")), 1.2f);
            }
            PlaySfx(TEXT("bell"), 0.6f, 1.6f);
        }
    }
    PlaySfx(TEXT("grunt"));
    if (Bandit == Boss)
    {
        bBossBeaten = true;
        ++BossesBeaten;
        AddScore(500, By);
        if (ShowdownStep == 4)
        {
            // Measured on the shooter's own screen from when DRAW! reached it, so a slow line does not cost them.
            const float Ms = By && By->LastReactionMs >= 0 ? float(By->LastReactionMs) : float((GetWorld()->GetTimeSeconds() - DrawCalledAt) * 1000.0);
            DrawTimeMs = DrawTimeMs < 0.0f ? Ms : FMath::Min(DrawTimeMs, Ms);      // best draw of the run
            if (PS) { PS->BestDrawMs = PS->BestDrawMs < 0 ? int32(Ms) : FMath::Min(PS->BestDrawMs, int32(Ms)); }
            AddScore(FMath::Max(0, 1000 - int32(Ms / 2.0f)), By);
        }
        Prompt = TEXT("");
        ShowdownStep = 6;
        ShowdownTimer = 3.0f;
    }
}

float AFGIronHorseGameMode::JudgeDelay(const AFGTrainPlayer* P) const
{
    // A partner over the network sees a bullet half a round trip late, and their dodge reaches here half a round
    // trip late. Wait that round trip before judging, so a dodge made in time on their own screen counts.
    if (!P || P->IsLocallyControlled()) { return 0.0f; }
    const APlayerState* PS = P->GetPlayerState();
    return PS ? FMath::Clamp(PS->GetPingInMilliseconds() / 1000.0f, 0.0f, 0.25f) : 0.1f;
}

void AFGIronHorseGameMode::SpawnEnemyShot(const FVector& From, bool bFast, AFGTrainPlayer* Victim, float ExtraDelay)
{
    // Aimed at where the head is now. It takes 0.7 s to arrive: move and it misses.
    if (!Victim || Victim->bDowned) { return; }
    FFGEnemyShot& Shot = Shots.AddDefaulted_GetRef();
    Shot.Victim = Victim;
    Shot.bAccurate = bFast || FMath::FRand() < 0.5f;
    Shot.Target = Victim->HeadLocation();
    if (!Shot.bAccurate)
    {
        const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
        Shot.Target += FVector(0.0f, FMath::Cos(Angle), FMath::Sin(Angle) * 0.5f + 0.5f) * FMath::FRandRange(60.0f, 110.0f);
    }
    const float Flight = (bFast ? 0.3f : ShotFlight()) + ExtraDelay;
    Shot.TimeLeft = Flight + JudgeDelay(Victim);
    GS()->MulticastEnemyShot(From, Shot.Target, Flight);
}

void AFGIronHorseGameMode::SpawnDynamite(const FVector& From, AFGTrainPlayer* Victim)
{
    // Lobbed at a player. Shoot it in the air, or lean out of the blast.
    if (!Victim || Victim->bDowned)
    {
        const TArray<AFGTrainPlayer*> Alive = AlivePlayers();
        if (!Alive.Num()) { return; }
        Victim = Alive[0];
    }
    const float Flight = 2.0f;
    const FVector Target = Victim->HeadLocation() - FVector(0, 0, 60.0f);
    const float G = 980.0f;
    FVector V = (Target - From) / Flight;
    V.Z += 0.5f * G * Flight;
    AFGTarget* Stick = SpawnTarget(FGAssets::StaticMesh(TEXT("props"), TEXT("SM_Dynamite")), FTransform(FRotator::ZeroRotator, From, FVector(2.6f)), 55.0f);
    Stick->Throw(V, G);
    Stick->Fuse = Flight;
    Stick->OnShot = [this](AFGTarget* T, AFGTrainPlayer* By)
    {
        AddScore(150, By);
        PlaySfx(TEXT("boom"));
        GS()->MulticastFx(EFGFxKind::DynamiteShot, T->GetActorLocation(), TrainSpeed);
    };
    Stick->OnFuse = [this, Target](AFGTarget* T)
    {
        PlaySfx(TEXT("boom"));
        GS()->MulticastFx(EFGFxKind::DynamiteBlast, T->GetActorLocation(), TrainSpeed);
        for (AFGTrainPlayer* P : AlivePlayers())
        {
            const FVector Head = P->HeadLocation();
            if (FMath::Abs(Head.X - Target.X) > 150.0f) { continue; }      // the other end of the roof
            if (FMath::Abs(Head.Y - Target.Y) < 70.0f) { HurtPlayer(P, TEXT("hurt")); }
            else { AddDodge(P, 25); }
        }
    };
}

void AFGIronHorseGameMode::TickShots(float DeltaTime)
{
    for (int32 i = Shots.Num() - 1; i >= 0; --i)
    {
        FFGEnemyShot& Shot = Shots[i];
        Shot.TimeLeft -= DeltaTime;
        if (Shot.TimeLeft > 0.0f) { continue; }
        AFGTrainPlayer* P = Shot.Victim.Get();
        if (P && !P->bDowned)
        {
            const float Miss = FVector::Dist(P->HeadLocation(), Shot.Target);
            if (Shot.bAccurate && Miss < Shot.Radius)
            {
                HurtPlayer(P, TEXT("hurt"));
            }
            else
            {
                if (Shot.bAccurate) { AddDodge(P, 25); }
                PlaySfxFor(P, TEXT("whiz"), 0.9f, FMath::FRandRange(0.9f, 1.15f));
            }
        }
        Shots.RemoveAtSwap(i);
    }
}

void AFGIronHorseGameMode::HurtPlayer(AFGTrainPlayer* P, FName Sfx)
{
    if (!P || P->bDowned || bGod || P->InvulnerableFor > 0.0f || Phase == EFGPhase::Result) { return; }
    P->InvulnerableFor = 2.2f;
    P->ClientHurt(Sfx);
    const int32 Before = P->Hats;
    const bool bDead = P->TakeHit();
    if (Before == 3) { GS()->MulticastHatOff(P); }       // the first hit shoots your hat off. You see it go.
    if (bDead)
    {
        P->bDowned = true;
        P->bGunHidden = true;
        UE_LOG(LogTemp, Log, TEXT("IronHorse: %s is down"), *NameOf(P));
        if (AFGPlayerState* PS = P->FGPlayerState()) { ++PS->TimesDowned; }
        if (AlivePlayers().Num() == 0)
        {
            bTeamDown = true;
            SetPhase(EFGPhase::Result);
        }
        else
        {
            ShowBanner(FString::Printf(TEXT("%s IS DOWN.  A HEADSHOT BRINGS THEM BACK"), *NameOf(P)), 3.0f);
        }
    }
}

void AFGIronHorseGameMode::TickHazards()
{
    // Low things over the track: water tower spout, signal gantry, tunnel mouths. Each rider is judged where they
    // stand, when the thing reaches them.
    const float D = World->MetresTo(TEXT("duck"));
    bDuckWarning = D > 0.0f && D < FMath::Max(TrainSpeed, 8.0f) * 2.6f && TrainSpeed > 3.0f;
    // Under a tunnel roof (5.9 m, the standing eye is at 6.0) anyone not actually ducking is scraping along the
    // ceiling. That costs a hat every second and a half.
    bStayDown = World->MetresTo(TEXT("dark")) == 0.0f && TrainSpeed > 3.0f;
    const double S = World->GetDistance();
    for (AFGTrainPlayer* P : AlivePlayers())
    {
        const double Here = S + P->SeatLocation.X / 100.0;
        const FFGTrackerState In = P->InputState();
        if (P->LastHazardS >= 0.0 && Here > P->LastHazardS && TrainSpeed > 3.0f)
        {
            if (World->EventBetween(TEXT("duck"), P->LastHazardS, Here))
            {
                if (In.Duck < 0.45f) { HurtPlayer(P, TEXT("bonk")); }
                else { AddDodge(P, 50); PlaySfxFor(P, TEXT("whiz"), 1.0f, 0.6f); }
            }
            // Signal arms over one half of the roof: lean over to the other side of where you stand.
            float Side = 0.0f;
            if (World->LeanObstacleBetween(P->LastHazardS, Here, Side))
            {
                const float HeadY = float(P->HeadLocation().Y - P->SeatLocation.Y) * Side;
                if (HeadY > -30.0f) { HurtPlayer(P, TEXT("bonk")); }
                else { AddDodge(P, 50); PlaySfxFor(P, TEXT("whiz"), 1.0f, 0.6f); }
            }
        }
        P->LastHazardS = Here;
        if (bStayDown && In.Duck < 0.45f && PhaseTime > 0.0f) { HurtPlayer(P, TEXT("bonk")); }
    }
}

void AFGIronHorseGameMode::TickLeanObstacles(float DeltaTime)
{
    // Signal arms over one half of the roof: lean to the other side. Every 16-26 s of open running, never near a
    // tunnel, bridge, canyon or another duck, and never in a duel. (Who gets hit: TickHazards.)
    float Side = 0.0f;
    const float D = World->MetresToLeanObstacle(Side);
    const bool bRiding = Phase == EFGPhase::Ride && TrainSpeed > 10.0f;
    ObstacleTimer -= bRiding ? DeltaTime : 0.0f;
    const float Ahead = FMath::Max(TrainSpeed, 15.0f) * 4.5f;
    if (bRiding && ObstacleTimer <= 0.0f && D < 0.0f && AliveBandits() <= 1 && !World->EventsWithin(Ahead + 120.0f))
    {
        ObstacleTimer = FMath::FRandRange(16.0f, 26.0f);
        World->AddLeanObstacle(Ahead, FMath::RandBool() ? 1.0f : -1.0f);
    }
    // The arm is over the Side half, so the warning points the other way.
    LeanWarning = D > 0.0f && D < FMath::Max(TrainSpeed, 8.0f) * 2.8f ? -Side : 0.0f;
}

void AFGIronHorseGameMode::TickSky(float DeltaTime)
{
    // The day runs with the lap: golden hour at the station, the sun sinking dead ahead so the boss stands in front
    // of it, then night after he falls, and morning after the next one. (Drawn by AFGPresentation on every machine.)
    if (Phase == EFGPhase::Ride && NightTarget < 0.5f) { Dusk = FMath::Max(Dusk, FMath::Clamp((int32(Stage) * StageSeconds + StageTime) / (3.4f * StageSeconds), 0.0f, 0.9f)); }
    if (Phase == EFGPhase::Showdown && NightTarget < 0.5f)
    {
        Dusk = FMath::FInterpConstantTo(Dusk, 1.0f, DeltaTime, 0.08f);
        // For the duel the sun is steered, slowly, to stand dead ahead in the landscape as it is then (the line is
        // kept straight meanwhile), so the boss has it behind him.
        const float Want = 180.0f - World->WorldYaw();
        SunChainYaw += FMath::Clamp(FMath::FindDeltaAngleDegrees(SunChainYaw, Want), -14.0f * DeltaTime, 14.0f * DeltaTime);
    }
}

// ------------------------------------------------------------------ 1v1

AFGTrainPlayer* AFGIronHorseGameMode::Opponent(const AFGTrainPlayer* P) const
{
    for (AFGTrainPlayer* Other : Players()) { if (Other != P) { return Other; } }
    return nullptr;
}

void AFGIronHorseGameMode::StartVersus()
{
    UE_LOG(LogTemp, Log, TEXT("IronHorse: 1v1"));
    for (AFGBandit* B : Bandits) { if (B) { B->Destroy(); } }
    for (AFGTarget* T : Targets) { if (T) { T->Destroy(); } }
    Bandits.Reset();
    Targets.Reset();
    Shots.Reset();
    Boss = nullptr;
    Score = Kills = Headshots = Dodges = 0;
    Lap = 0;
    DrawTimeMs = -1.0f;
    DistanceM = 0.0;
    RideTime = ResultTime = 0.0f;
    bTeamDown = bStayDown = bDuckWarning = false;
    LeanWarning = 0.0f;
    for (AFGTrainPlayer* P : Players())
    {
        Revive(P, 3);
        P->InvulnerableFor = 0.0f;
        P->SetWeapon(0);
        P->ClientNewGun(0);
        if (AFGPlayerState* PS = P->FGPlayerState()) { PS->ResetTally(); }
    }
    GS()->bBellRaised = false;
    BanditTrain->SetActorHiddenInGame(true);
    // Open, straight, empty track, with as much of it behind the train as ahead: one of the two looks back down the line.
    ++GS()->LineEpoch;
    World->ResetLine();
    World->KeepBehindM = 450.0;
    World->bAllowRandomLandmarks = false;
    World->bStraightOnly = true;
    World->Queue({ TEXT("Flat_A"), TEXT("Flat_B"), TEXT("Cactus_A"), TEXT("Flat_A"), TEXT("Rocky_A") });
    World->SetDistance(450.0);
    Train->Place(World, 0.0f);
    // Late afternoon, the sun off to one side so neither has it in their eyes.
    NightTarget = 0.0f;
    Dusk = 0.55f;
    SunChainYaw = 95.0f;
    SetPhase(EFGPhase::Versus);
}

void AFGIronHorseGameMode::TickVersus(float DeltaTime)
{
    if (Players().Num() < 2)
    {
        // The other one left: back to waiting on the title.
        SetPhase(EFGPhase::Title);
        return;
    }
    VersusTimer -= DeltaTime;
    const double Now = GetWorld()->GetTimeSeconds();
    switch (VersusStep)
    {
    case 0:     // the rules, while the train gets going
        if (VersusTimer <= 0.0f)
        {
            SubPrompt = TEXT("");
            VersusStep = 1;
            HolsteredFor = HolsterWait = 0.0f;
        }
        break;
    case 1:     // both holster (the HUD tells each how: tracker or keys). A booth player who never finds it still gets their duel.
    {
        Prompt = TEXT("HOLSTER");
        bool bAll = true;
        for (const AFGTrainPlayer* P : Players()) { bAll &= P->InputState().bHolstered; }
        HolsteredFor = bAll ? HolsteredFor + DeltaTime : 0.0f;
        HolsterWait += DeltaTime;
        if (HolsteredFor > 0.6f || HolsterWait > 6.0f)
        {
            Prompt = TEXT("WAIT FOR IT...");
            VersusTimer = FMath::FRandRange(1.8f, 3.4f);
            VersusStep = 2;
            PlaySfx(TEXT("heartbeat"));
        }
        break;
    }
    case 2:     // the wait. A shot now loses the round (ResolveVersusShot).
        if (VersusTimer <= 0.0f)
        {
            Prompt = TEXT("DRAW!");
            PlaySfx(TEXT("whistle"));
            DrawCalledAt = Now;
            PendingWinner = nullptr;
            VersusTimer = 6.0f;
            VersusStep = 3;
        }
        break;
    case 3:     // live
        if (PendingWinner.IsValid() && Now >= PendingUntil)
        {
            WinRound(PendingWinner.Get(), PendingMs, false);
        }
        else if (!PendingWinner.IsValid() && VersusTimer <= 0.0f)
        {
            ShowBanner(TEXT("NOBODY HIT.  AGAIN"), 2.0f);
            Prompt = TEXT("");
            VersusStep = 4;
            VersusTimer = 2.0f;
        }
        break;
    case 4:     // after a round
        if (VersusTimer <= 0.0f)
        {
            bool bOver = false;
            for (const AFGTrainPlayer* P : Players()) { bOver |= P->Hats <= 0; }
            if (bOver) { SetPhase(EFGPhase::Result); }
            else { VersusStep = 1; HolsteredFor = HolsterWait = 0.0f; }
        }
        break;
    }
}

bool AFGIronHorseGameMode::ResolveVersusShot(AFGTrainPlayer* P, const FVector& Origin, const FVector& Dir, const FVector& Muzzle)
{
    AFGTrainPlayer* Them = Opponent(P);
    if (!Them) { return false; }
    // Against the cowboy the shooter is looking at: his chest and head bones. A little help for webcam aim, much less
    // than against bandits, so leaning out of the line of fire works.
    const USkeletalMeshComponent* Body = Them->GetMesh();
    const bool bBones = Body && Body->DoesSocketExist(TEXT("head")) && Body->DoesSocketExist(TEXT("spine_02"));
    const FVector Chest = bBones ? Body->GetSocketLocation(TEXT("spine_02")) : Them->GetActorLocation() + FVector(0, 0, 30.0f);
    const FVector Head = bBones ? Body->GetSocketLocation(TEXT("head")) + FVector(0, 0, 10.0f) : Them->HeadLocation();
    const float Dist = FMath::Max(100.0f, float(FVector::Dist(Origin, Chest)));
    auto Angle = [&Dir, &Origin](const FVector& To) { return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, (To - Origin).GetSafeNormal()), -1.0, 1.0))); };
    const bool bHead = Angle(Head) < FMath::RadiansToDegrees(FMath::Atan(18.0f / Dist)) + 0.5f;
    const bool bChest = Angle(Chest) < FMath::Max(2.0f, FMath::RadiansToDegrees(FMath::Atan(32.0f / Dist)));
    const bool bHit = VersusStep == 3 && (bHead || bChest);
    const FVector Point = bHit ? (bHead ? Head : Chest) : Origin + Dir * 4000.0f;
    GS()->MulticastPlayerShot(P, Muzzle, Point, uint8(P->Weapon().Tracers), P->Weapon().SfxPitch);

    if (VersusStep == 2)
    {
        if (AFGPlayerState* PS = P->FGPlayerState()) { ++PS->Fouls; }
        WinRound(Them, -1, true);
        return false;
    }
    if (!bHit) { return false; }
    GS()->MulticastFx(EFGFxKind::HitDust, Point, 0.0f);
    // Fastest draw wins, measured on each shooter's own screen from when DRAW! reached it, so a slower line does not
    // decide it. The first hit to arrive waits a round trip for a quicker one from across the network.
    const int32 Ms = P->LastReactionMs >= 0 ? P->LastReactionMs : int32((GetWorld()->GetTimeSeconds() - DrawCalledAt) * 1000.0);
    if (!PendingWinner.IsValid())
    {
        float Window = 0.05f;
        for (const AFGTrainPlayer* Each : Players())
        {
            const APlayerState* EachPS = Each->GetPlayerState();
            if (!Each->IsLocallyControlled() && EachPS) { Window = FMath::Max(Window, FMath::Min(0.3f, EachPS->GetPingInMilliseconds() / 1000.0f)); }
        }
        PendingUntil = GetWorld()->GetTimeSeconds() + Window;
        PendingWinner = P;
        PendingMs = Ms;
    }
    else if (PendingWinner.Get() != P && Ms < PendingMs)
    {
        PendingWinner = P;
        PendingMs = Ms;
    }
    return true;
}

void AFGIronHorseGameMode::WinRound(AFGTrainPlayer* Winner, int32 Ms, bool bFoul)
{
    AFGTrainPlayer* Loser = Opponent(Winner);
    PendingWinner = nullptr;
    Prompt = TEXT("");
    VersusStep = 4;
    VersusTimer = 3.5f;
    if (AFGPlayerState* PS = Winner->FGPlayerState())
    {
        ++PS->Points;
        if (!bFoul && Ms >= 0) { PS->BestDrawMs = PS->BestDrawMs < 0 ? Ms : FMath::Min(PS->BestDrawMs, Ms); }
    }
    if (!bFoul && Ms >= 0) { DrawTimeMs = DrawTimeMs < 0.0f ? float(Ms) : FMath::Min(DrawTimeMs, float(Ms)); }
    if (Loser)
    {
        Loser->ClientHurt(TEXT("hurt"));
        GS()->MulticastHatOff(Loser);
        Loser->SetHats(Loser->Hats - 1);
        if (Loser->Hats <= 0)
        {
            Loser->bDowned = true;          // he goes down; the poster follows
            Loser->bGunHidden = true;
        }
    }
    PlaySfx(TEXT("grunt"));
    ShowBanner(bFoul ? FString::Printf(TEXT("%s DREW EARLY.  %s TAKES THE ROUND"), *NameOf(Loser), *NameOf(Winner))
                     : FString::Printf(TEXT("%s WINS THE ROUND   %d ms"), *NameOf(Winner), Ms), 3.0f);
    UE_LOG(LogTemp, Log, TEXT("IronHorse: 1v1 round to %s (%s %d ms), hats left %d"), *NameOf(Winner), bFoul ? TEXT("foul") : TEXT("draw"), Ms, Loser ? Loser->Hats : -1);
}

void AFGIronHorseGameMode::PlaySfx(FName Name, float Volume, float Pitch)
{
    GS()->MulticastSfx(Name, Volume, Pitch);
}

void AFGIronHorseGameMode::PlaySfxFor(AFGTrainPlayer* P, FName Name, float Volume, float Pitch)
{
    if (AFGPlayerController* PC = P ? Cast<AFGPlayerController>(P->GetController()) : nullptr) { PC->ClientSfx(Name, Volume, Pitch); }
}

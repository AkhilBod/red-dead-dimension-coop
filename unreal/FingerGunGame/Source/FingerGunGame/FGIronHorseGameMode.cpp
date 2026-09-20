#include "FGIronHorseGameMode.h"

#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "FGAssets.h"
#include "FGBandit.h"
#include "FGFx.h"
#include "FGHud.h"
#include "FGTarget.h"
#include "FGTrackerInput.h"
#include "FGTrain.h"
#include "FGTrainPlayer.h"
#include "FGWorldStreamer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
    constexpr float CruiseSpeed = 26.0f;            // m/s
    constexpr float StartDistance = 58.0f;          // metres: alongside the station platform
    constexpr float PlayerForwardCm = 300.0f;       // how far up the passenger car roof the player stands
    constexpr float RidersUntil = 58.0f;            // seconds after departure
    constexpr float BoardersUntil = 100.0f;
    constexpr float SecondTrainUntil = 146.0f;
    constexpr float ShowdownAt = 150.0f;
    constexpr int32 MaxTokens = 2;
    constexpr float AimAssistDegrees = 7.0f;        // how far off a shot may be and still hit
    constexpr float SideTrackCm = -700.0f;          // enemy line is 7 m to the driver's left

    const FVector2D CalibScreen[4] = { {0.22, 0.30}, {0.78, 0.30}, {0.78, 0.68}, {0.22, 0.68} };

    float AngleBetween(const FVector& Dir, const FVector& To)
    {
        return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, To.GetSafeNormal()), -1.0, 1.0)));
    }
}

AFGIronHorseGameMode::AFGIronHorseGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = AFGTrainPlayer::StaticClass();
    HUDClass = AFGHud::StaticClass();
}

APawn* AFGIronHorseGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform&)
{
    // Whatever the level's PlayerStart says, the player stands on the passenger car roof at the origin facing up the train.
    const FTransform Roof(FRotator::ZeroRotator, FVector(PlayerForwardCm, 0.0f, AFGTrain::RoofCm + 96.0f));
    return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, Roof);
}

void AFGIronHorseGameMode::BeginPlay()
{
    Super::BeginPlay();
    UWorld* W = GetWorld();

    BuildSky();

    World = W->SpawnActor<AFGWorldStreamer>();
    World->Queue({ TEXT("Flat_A"), TEXT("Landmark_Station_A"), TEXT("Flat_B"), TEXT("Rocky_A"), TEXT("Landmark_WaterTower_A"), TEXT("Cactus_A"), TEXT("CurveL_A"), TEXT("Mesa_A"), TEXT("CurveR_A") });
    World->bAllowRandomLandmarks = false;
    World->SetDistance(StartDistance);

    BuildTrains();

    Player = Cast<AFGTrainPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));

    if (USoundBase* Loop = FGAssets::Sound(TEXT("train_loop")))
    {
        TrainLoop = UGameplayStatics::SpawnSound2D(this, Loop, 0.0f, 1.0f, 0.0f, nullptr, false, false);
    }

    // Two people playing a webcam game on a laptop: spend the GPU on frame rate, not on ray tracing.
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
    {
        for (const TCHAR* Cmd : { TEXT("r.Lumen.HardwareRayTracing 0"), TEXT("r.RayTracing.Shadows 0"), TEXT("r.MotionBlurQuality 2"), TEXT("r.VolumetricFog 0"), TEXT("r.Shadow.Virtual.Enable 0"), TEXT("r.SetNearClipPlane 4"), TEXT("DisableAllScreenMessages"), TEXT("t.MaxFPS 60") })
        {
            PC->ConsoleCommand(Cmd);
        }
    }
    // Test switches: -FGAuto plays by itself, -FGGod takes no damage, -FGShots=4 saves a screenshot every 4 s, -FGSkip=95 jumps into the ride.
    bAutoPlay = FParse::Param(FCommandLine::Get(), TEXT("FGAuto"));
    bGod = FParse::Param(FCommandLine::Get(), TEXT("FGGod"));
    FParse::Value(FCommandLine::Get(), TEXT("FGShots="), ShotEvery);
    FParse::Value(FCommandLine::Get(), TEXT("FGSkip="), SkipTo);
    SetPhase(EFGPhase::Title);
}

void AFGIronHorseGameMode::TickTest(float DeltaTime)
{
    if (ShotEvery > 0.0f)
    {
        ShotTimer -= DeltaTime;
        if (ShotTimer <= 0.0f)
        {
            ShotTimer = ShotEvery;
            FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/fg_%03d.png"), ShotIndex++)), true, false);
        }
    }
    if (!bAutoPlay) { return; }
    if (Phase == EFGPhase::Title && PhaseTime > 1.0f) { SetPhase(EFGPhase::Tutorial); }
    AutoTimer -= DeltaTime;
    if (AutoTimer > 0.0f) { return; }
    AutoTimer = 0.7f;
    if (Player->GetCurrentAmmo() <= 0) { Player->Tracker->OnReload.Broadcast(); return; }
    FVector Aim = FVector::ZeroVector;
    for (AFGTarget* T : Targets) { if (T && T->bActive) { Aim = T->Centre(); break; } }
    if (Aim.IsZero())
    {
        for (AFGBandit* B : Bandits)
        {
            if (B && !B->IsDead() && B->State != EFGBanditState::Entering) { FVector Chest, Head; B->AimPoints(Chest, Head); Aim = Chest; break; }
        }
    }
    if (Phase == EFGPhase::Result) { if (ResultTime > 6.0f) { Player->Tracker->OnFire.Broadcast(RideAgainButton().GetCenter()); } return; }
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    FVector2D Screen;
    int32 VW, VH;
    PC->GetViewportSize(VW, VH);
    if (!Aim.IsZero() && PC->ProjectWorldLocationToScreen(Aim, Screen) && VW > 0)
    {
        Player->Tracker->State.AimX = Screen.X / VW;
        Player->Tracker->State.AimY = Screen.Y / VH;
        Player->Tracker->OnFire.Broadcast(FVector2D(Screen.X / VW, Screen.Y / VH));
    }
}

void AFGIronHorseGameMode::BuildSky()
{
    UWorld* W = GetWorld();
    if (TActorIterator<ADirectionalLight> It(W); It)
    {
        Sun = *It;      // the level brought its own lighting: leave it alone
        SunIntensity = Sun->GetLightComponent()->Intensity;
        for (TActorIterator<ASkyLight> SkyIt(W); SkyIt; ++SkyIt) { Sky = *SkyIt; }
        return;
    }
    // Golden hour: low warm sun ahead and to the left, long shadows, haze.
    Sun = W->SpawnActor<ADirectionalLight>(FVector(0, 0, 3000), FRotator(-16.0f, 150.0f, 0.0f));
    UDirectionalLightComponent* SunComp = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
    SunComp->SetMobility(EComponentMobility::Movable);
    SunComp->SetIntensity(SunIntensity);
    SunComp->SetLightColor(FLinearColor(1.0f, 0.80f, 0.58f));
    SunComp->SetAtmosphereSunLight(true);
    SunComp->DynamicShadowDistanceMovableLight = 30000.0f;

    W->SpawnActor<AActor>(ASkyAtmosphere::StaticClass(), FTransform::Identity);

    Sky = W->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->SetRealTimeCapture(true);
    Sky->GetLightComponent()->SetIntensity(1.3f);

    AExponentialHeightFog* Fog = W->SpawnActor<AExponentialHeightFog>();
    UExponentialHeightFogComponent* FogComp = Fog->GetComponent();
    FogComp->SetFogDensity(0.018f);
    FogComp->SetFogHeightFalloff(0.08f);
    FogComp->SetFogInscatteringColor(FLinearColor(0.85f, 0.55f, 0.32f));
    FogComp->SetStartDistance(3000.0f);

    APostProcessVolume* PP = W->SpawnActor<APostProcessVolume>();
    PP->bUnbound = true;
    FPostProcessSettings& S = PP->Settings;
    S.bOverride_VignetteIntensity = true;       S.VignetteIntensity = 0.65f;
    S.bOverride_FilmGrainIntensity = true;      S.FilmGrainIntensity = 0.35f;
    S.bOverride_WhiteTemp = true;               S.WhiteTemp = 7300.0f;
    S.bOverride_ColorSaturation = true;         S.ColorSaturation = FVector4(0.92f, 0.90f, 0.86f, 1.0f);
    S.bOverride_ColorContrast = true;           S.ColorContrast = FVector4(1.08f, 1.08f, 1.08f, 1.0f);
    S.bOverride_BloomIntensity = true;          S.BloomIntensity = 0.9f;
    S.bOverride_MotionBlurAmount = true;        S.MotionBlurAmount = 0.35f;
    S.bOverride_AutoExposureMinBrightness = true; S.AutoExposureMinBrightness = 0.6f;
    S.bOverride_AutoExposureMaxBrightness = true; S.AutoExposureMaxBrightness = 1.6f;
}

void AFGIronHorseGameMode::BuildTrains()
{
    UWorld* W = GetWorld();
    Train = W->SpawnActor<AFGTrain>();
    Train->Build({ TEXT("Locomotive"), TEXT("Tender"), TEXT("Boxcar"), TEXT("PassengerCar"), TEXT("Flatcar_Crates"), TEXT("Caboose") }, 3, false);
    Train->Place(World, 0.0f);

    BanditTrain = W->SpawnActor<AFGTrain>();
    BanditTrain->Build({ TEXT("Locomotive"), TEXT("Tender"), TEXT("Boxcar"), TEXT("PassengerCar"), TEXT("Flatcar_Crates"), TEXT("Caboose") }, 3, true);
    BanditTrain->LateralCm = SideTrackCm;
    BanditTrain->Offset = -400.0;
    BanditTrain->SetActorHiddenInGame(true);

    // A lantern on the player's car, so the tunnel is dark but not black.
    Lantern = NewObject<UPointLightComponent>(Train);
    Lantern->SetMobility(EComponentMobility::Movable);
    Lantern->SetupAttachment(Train->GetRootComponent());
    Lantern->SetRelativeLocation(FVector(PlayerForwardCm + 250.0f, 0.0f, AFGTrain::RoofCm + 230.0f));
    Lantern->SetIntensityUnits(ELightUnits::Candelas);
    Lantern->SetIntensity(0.0f);
    Lantern->SetLightColor(FLinearColor(1.0f, 0.62f, 0.28f));
    Lantern->SetAttenuationRadius(2600.0f);
    Lantern->SetCastShadows(false);
    Lantern->RegisterComponent();
}

// ------------------------------------------------------------------ phases

void AFGIronHorseGameMode::SetPhase(EFGPhase NewPhase)
{
    Phase = NewPhase;
    PhaseTime = 0.0f;
    UE_LOG(LogTemp, Log, TEXT("IronHorse: phase %d, score %d"), int32(NewPhase), Score);
    UFGTrackerInput* Tracker = Player ? Player->Tracker.Get() : nullptr;
    switch (Phase)
    {
    case EFGPhase::Title:
        Prompt = TEXT("MAKE A FINGER GUN");
        SubPrompt = TEXT("point it at the screen");
        bCrosshairVisible = false;
        break;
    case EFGPhase::Calibrate:
        if (Tracker) { Tracker->SendCalibBegin(); }
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
        RideTime = 0.0f;
        SpawnTimer = 14.0f;
        break;
    case EFGPhase::Showdown:
        EveryoneLeave();
        ShowdownStep = 0;
        ShowdownTimer = 3.0f;
        Prompt = TEXT("");
        break;
    case EFGPhase::Result:
        EveryoneLeave();
        ResultTime = 0.0f;
        bCrosshairVisible = true;
        Prompt = TEXT("");
        SubPrompt = TEXT("");
        break;
    }
}

FVector AFGIronHorseGameMode::ScreenToWorldPoint(FVector2D Screen, float DistanceCm) const
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    int32 W = 0, H = 0;
    FVector Origin, Dir;
    if (PC) { PC->GetViewportSize(W, H); }
    if (PC && W > 0 && PC->DeprojectScreenPositionToWorld(Screen.X * W, Screen.Y * H, Origin, Dir))
    {
        return Origin + Dir * DistanceCm;
    }
    return (Player ? Player->HeadLocation() : FVector::ZeroVector) + FVector(DistanceCm, (Screen.X - 0.5f) * DistanceCm, (0.5f - Screen.Y) * DistanceCm);
}

AFGTarget* AFGIronHorseGameMode::SpawnTarget(const FString& Folder, const FString& Mesh, const FTransform& At, float Radius)
{
    AFGTarget* T = GetWorld()->SpawnActor<AFGTarget>(AFGTarget::StaticClass(), At);
    if (!Mesh.IsEmpty()) { T->Mesh->SetStaticMesh(FGAssets::StaticMesh(Folder, Mesh)); }
    T->SetActorScale3D(At.GetScale3D());
    T->Radius = Radius;
    Targets.Add(T);
    return T;
}

void AFGIronHorseGameMode::SpawnCalibBottle()
{
    const FVector2D Screen = CalibScreen[CalibIndex];
    const FVector At = ScreenToWorldPoint(Screen, 750.0f);
    const float Scale = 2.6f;
    AFGTarget* Bottle = SpawnTarget(TEXT("props"), CalibIndex % 2 ? TEXT("SM_Bottle_Brown") : TEXT("SM_Bottle_Green"), FTransform(FRotator::ZeroRotator, At - FVector(0, 0, 15.0f * Scale), FVector(Scale)), 60.0f);
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
    if (Player) { Player->Tracker->SendCalibTarget(Screen.X, Screen.Y); }
    Bottle->OnShot = [this](AFGTarget*)
    {
        Score += 10;
        PlaySfx(TEXT("glass"));
        if (++CalibIndex < 4) { SpawnCalibBottle(); }
        else { SetPhase(EFGPhase::Tutorial); }
    };
}

void AFGIronHorseGameMode::SpawnCans()
{
    // Two cans on the boxcar roof ahead: enough to learn the trigger, then the bell.
    CansLeft = 2;
    for (int32 i = 0; i < 2; ++i)
    {
        const FVector Base(PlayerForwardCm + 640.0f + i * 40.0f, (i * 2 - 1) * 90.0f, AFGTrain::RoofCm);
        AFGTarget* Crate = SpawnTarget(TEXT("props"), TEXT("SM_Crate"), FTransform(FRotator(0, i * 17.0f, 0), Base), 0.0f);
        Crate->bActive = false;
        const float Scale = 3.2f;
        AFGTarget* Can = SpawnTarget(TEXT("props"), TEXT("SM_TinCan"), FTransform(FRotator::ZeroRotator, Base + FVector(0, 0, 90.0f), FVector(Scale)), 28.0f);
        Can->CentreOffset = FVector(0, 0, 5.5f * Scale);
        Can->OnShot = [this, Crate](AFGTarget*)
        {
            Score += 10;
            PlaySfx(TEXT("ricochet"));
            if (--CansLeft <= 0) { SetPhase(EFGPhase::Bell); }
        };
    }
}

void AFGIronHorseGameMode::SpawnBell()
{
    AFGTarget* Bell = SpawnTarget(TEXT(""), TEXT(""), FTransform(FVector(1200.0f, 420.0f, 300.0f)), 110.0f);
    Bell->bBreaks = false;
    USkeletalMeshComponent* Rig = World ? World->FindRig(TEXT("StationBell")) : nullptr;
    if (Rig)
    {
        // As modelled the bell hangs 2.5 m up, far below a player on the roof. A taller post puts it near eye level.
        const float Tall = 2.3f;
        Rig->SetRelativeScale3D(FVector(Tall));
        Bell->SetActorLocation(Rig->GetComponentLocation());
        Bell->CentreOffset = FVector(0, 0, 250.0f * Tall);
        Bell->Radius = 160.0f;
    }
    else
    {
        // No station in view: hang a start sign in the middle of the screen instead.
        Bell->Mesh->SetStaticMesh(FGAssets::StaticMesh(TEXT("props"), TEXT("SM_StartSign")));
        Bell->SetActorLocation(ScreenToWorldPoint(FVector2D(0.5, 0.45), 900.0f));
        Bell->SetActorRotation(FRotator(0, 90.0f, 0));
        Bell->CentreOffset = FVector(0, 0, 120.0f);
    }
    Bell->OnShot = [this, Rig](AFGTarget* T)
    {
        if (Rig)
        {
            if (UAnimSequence* Ring = FGAssets::Anim(TEXT("setpieces"), TEXT("SK_StationBell"), TEXT("ring"))) { Rig->PlayAnimation(Ring, false); }
        }
        T->bActive = false;
        T->SetLifeSpan(Rig ? 0.1f : 1.0f);
        Depart();
    };
}

void AFGIronHorseGameMode::Depart()
{
    PlaySfx(TEXT("bell"));
    PlaySfx(TEXT("whistle"));
    for (AFGTarget* T : Targets) { if (T && T->bActive == false && T->Radius == 0.0f) { T->SetLifeSpan(6.0f); } }
    SetPhase(EFGPhase::Ride);
}

// ------------------------------------------------------------------ tick

void AFGIronHorseGameMode::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!Player || !World) { return; }
    PhaseTime += DeltaTime;
    HitMarker = FMath::Max(0.0f, HitMarker - DeltaTime * 4.0f);
    InvulnerableFor = FMath::Max(0.0f, InvulnerableFor - DeltaTime);
    Bandits.RemoveAll([](const AFGBandit* B) { return !IsValid(B); });
    Targets.RemoveAll([](const AFGTarget* T) { return !IsValid(T); });

    // The train
    const bool bMoving = Phase == EFGPhase::Ride || Phase == EFGPhase::Showdown || (Phase == EFGPhase::Result && TrainSpeed > 0.0f);
    const float WantSpeed = !bMoving ? 0.0f : (Phase == EFGPhase::Result ? CruiseSpeed * 0.6f : CruiseSpeed);
    TrainSpeed = FMath::FInterpConstantTo(TrainSpeed, WantSpeed, DeltaTime, 2.6f);
    World->SetDistance(World->GetDistance() + TrainSpeed * DeltaTime);
    Train->Place(World, TrainSpeed);
    if (!BanditTrain->IsHidden()) { BanditTrain->Place(World, TrainSpeed); }
    Player->Rumble = TrainSpeed / CruiseSpeed;
    if (TrainLoop) { TrainLoop->SetVolumeMultiplier(0.15f + 0.85f * Player->Rumble); TrainLoop->SetPitchMultiplier(0.6f + 0.5f * Player->Rumble); }

    // Steam from the stack
    SteamTimer -= DeltaTime;
    if (SteamTimer <= 0.0f)
    {
        SteamTimer = TrainSpeed > 1.0f ? 0.11f : 0.5f;
        if (USkeletalMeshComponent* Loco = Train->Car(0))
        {
            const FVector Stack = Loco->GetComponentTransform().TransformPosition(FVector(0.0f, 330.0f, 440.0f));
            AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_Puff_Steam"), FTransform(FRotator(0, FMath::FRandRange(0.f, 360.f), 0), Stack, FVector(0.3f)),
                1.8f, FVector(-TrainSpeed * 80.0f, FMath::FRandRange(-40.f, 40.f), 520.0f), 1.3f);
        }
    }

    switch (Phase)
    {
    case EFGPhase::Title:
        if (PhaseTime > 2.5f && Player->Tracker->State.bAimValid && Player->Tracker->State.bGunPose) { SetPhase(EFGPhase::Tutorial); }
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
    case EFGPhase::Result:
        ResultTime += DeltaTime;
        break;
    }

    // Out of rounds: say so, whatever else is going on.
    if (Phase != EFGPhase::Result && Phase != EFGPhase::Title)
    {
        if (Player->GetCurrentAmmo() <= 0) { SubPrompt = Player->Tracker->bTrackerLive ? TEXT("EMPTY!  SLAP YOUR GUN HAND TO RELOAD") : TEXT("EMPTY!  PRESS R TO RELOAD"); }
        else if (SubPrompt.StartsWith(TEXT("EMPTY"))) { SubPrompt = TEXT(""); }
    }

    TickTest(DeltaTime);
    if (SkipTo > 0.0f && Phase == EFGPhase::Ride) { RideTime = SkipTo; SkipTo = 0.0f; TrainSpeed = 26.0f; }
    TickMagnet(DeltaTime);
    TickShots(DeltaTime);
    TickDuck();
    TickAtmosphere(DeltaTime);
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
    SpawnTimer -= DeltaTime;
    const bool bNarrow = World->MetresTo(TEXT("narrow")) == 0.0f || World->MetresTo(TEXT("dark")) == 0.0f || World->MetresTo(TEXT("trestle")) == 0.0f;

    // Tunnels are a ducking section, nothing else: nobody new shows up from 150 m out, and whoever is still
    // around clears off at the mouth. A bandit cannot ride or climb aboard inside a tunnel anyway.
    const float ToDark = World->MetresTo(TEXT("dark"));
    const bool bTunnelNear = ToDark >= 0.0f && ToDark < 150.0f;
    if (ToDark == 0.0f && !bInTunnel) { EveryoneLeave(); }
    bInTunnel = ToDark == 0.0f;
    if (bTunnelNear) { SpawnTimer = FMath::Max(SpawnTimer, 1.5f); }

    // The line ahead is laid 600 m out, so set pieces are ordered about 25 s before they are needed.
    if (!bQueuedTunnel && RideTime > 44.0f)
    {
        bQueuedTunnel = true;
        World->Queue({ TEXT("Tunnel_Entry_A"), TEXT("Tunnel_Mid_A"), TEXT("Tunnel_CurveL_A"), TEXT("Tunnel_Mid_A"), TEXT("Tunnel_Exit_A"), TEXT("Flat_A") });
    }
    if (!bQueuedSideTrack && RideTime > BoardersUntil - 24.0f)
    {
        bQueuedSideTrack = true;
        TArray<FString> Line = { TEXT("SideTrack_Start_A") };
        const TCHAR* Pattern[] = { TEXT("SideTrack_Mid_A"), TEXT("SideTrack_Mid_A"), TEXT("SideTrack_CurveL_A"), TEXT("SideTrack_Mid_A"), TEXT("SideTrack_CurveR_A"), TEXT("SideTrack_Mid_A") };
        for (int32 i = 0; i < 26; ++i) { Line.Add(Pattern[i % 6]); }
        Line.Add(TEXT("SideTrack_End_A"));
        World->Queue(Line);
    }

    if (RideTime < RidersUntil)
    {
        // Riders, both sides. Not in tunnels or on trestles: there is nowhere to ride.
        if (SpawnTimer <= 0.0f && AliveBandits() < 3 && !bNarrow)
        {
            SpawnTimer = RideTime < 30.0f ? 5.0f : 3.2f;
            FFGBanditSpec Spec;
            Spec.Kind = EFGBanditKind::Rider;
            const float Side = SpawnCount % 2 ? -1.0f : 1.0f;
            Spec.Slot = FVector(FMath::FRandRange(2400.0f, 3800.0f), Side * FMath::FRandRange(620.0f, 880.0f), 0.0f);
            Spec.Scale = 1.35f;        // out to the side they read small. Bigger and closer.
            Spec.Mesh = SpawnCount % 5 == 4 ? TEXT("SK_Gunslinger") : (SpawnCount % 3 == 2 ? TEXT("SK_Deputy") : TEXT("SK_Bandit"));
            Spec.HorseCoat = SpawnCount;
            Spec.FirstShotDelay = FMath::FRandRange(1.5f, 3.0f);
            SpawnBandit(Spec);
        }
    }
    else if (RideTime < BoardersUntil)
    {
        if (RideTime - DeltaTime < RidersUntil) { EveryoneLeave(); SpawnTimer = 2.0f; }
        if (SpawnTimer <= 0.0f && AliveBandits() < 3)
        {
            SpawnTimer = 3.0f;
            FFGBanditSpec Spec;
            Spec.Kind = EFGBanditKind::Boarder;
            static const FVector Slots[] = { {1400, -45, 0}, {1650, 50, 0}, {1900, -35, 0}, {1500, 55, 0}, {1800, -50, 0} };
            Spec.Slot = Slots[SpawnCount % 5] + FVector(0, 0, AFGTrain::RoofCm);
            Spec.Mesh = SpawnCount % 4 == 3 ? TEXT("SK_Heavy") : (SpawnCount % 2 ? TEXT("SK_Bandit") : TEXT("SK_Deputy"));
            Spec.Health = Spec.Mesh == TEXT("SK_Heavy") ? 50.0f : 25.0f;
            Spec.FirstShotDelay = FMath::FRandRange(1.0f, 2.2f);
            SpawnBandit(Spec);
        }
    }
    else if (RideTime < SecondTrainUntil)
    {
        if (RideTime - DeltaTime < BoardersUntil) { EveryoneLeave(); }
        // The bandit train pulls alongside once there is a second line to run on.
        if (BanditTrain->IsHidden() && World->MetresTo(TEXT("side_track")) == 0.0f)
        {
            BanditTrain->SetActorHiddenInGame(false);
            BanditTrain->Offset = -260.0;
            PlaySfx(TEXT("whistle"), 0.7f, 0.8f);
        }
        if (!BanditTrain->IsHidden())
        {
            BanditTrain->Offset = FMath::FInterpTo(BanditTrain->Offset, 24.0, DeltaTime, 0.55f);
            if (!bBanditTrainCrewed && BanditTrain->Offset > -10.0)
            {
                bBanditTrainCrewed = true;
                SpawnTimer = 0.0f;
            }
            if (bBanditTrainCrewed && SpawnTimer <= 0.0f && AliveBandits() < 4)
            {
                SpawnTimer = 2.6f;
                // Roofs of the boxcar (2), passenger car (3) and the flatcar's crates (4).
                struct FSeat { int32 Car; FVector Local; };
                static const FSeat Seats[] = { {3, {350, 0, 0}}, {2, {-150, 0, 0}}, {3, {-400, 30, 0}}, {2, {300, -20, 0}}, {4, {0, 0, -130}}, {3, {0, -30, 0}} };
                const FSeat& Seat = Seats[SpawnCount % 6];
                FFGBanditSpec Spec;
                Spec.Kind = SpawnCount % 4 == 2 ? EFGBanditKind::Dynamiter : EFGBanditKind::TrainShooter;
                Spec.Mesh = Spec.Kind == EFGBanditKind::Dynamiter ? TEXT("SK_Dynamiter") : TEXT("SK_Rifleman");
                Spec.Slot = Seat.Local + FVector(0, 0, AFGTrain::RoofCm);
                const int32 CarIndex = Seat.Car;
                Spec.Anchor = [this, CarIndex]() { return World->TrackWorld(BanditTrain->Offset + BanditTrain->CarCentre(CarIndex), BanditTrain->LateralCm); };
                Spec.FirstShotDelay = FMath::FRandRange(1.2f, 2.5f);
                SpawnBandit(Spec);
            }
        }
    }
    else
    {
        if (RideTime - DeltaTime < SecondTrainUntil) { EveryoneLeave(); }
        BanditTrain->Offset = FMath::FInterpTo(BanditTrain->Offset, -500.0, DeltaTime, 0.35f);
        if (BanditTrain->Offset < -350.0) { BanditTrain->SetActorHiddenInGame(true); }
        if (RideTime > ShowdownAt) { SetPhase(EFGPhase::Showdown); }
    }
}

void AFGIronHorseGameMode::TickShowdown(float DeltaTime)
{
    if (!BanditTrain->IsHidden())
    {
        BanditTrain->Offset = FMath::FInterpTo(BanditTrain->Offset, -500.0, DeltaTime, 0.35f);
        if (BanditTrain->Offset < -350.0) { BanditTrain->SetActorHiddenInGame(true); }
    }
    ShowdownTimer -= DeltaTime;
    const FFGTrackerState& In = Player->Tracker->State;
    switch (ShowdownStep)
    {
    case 0:     // quiet, then he lands
        if (ShowdownTimer <= 0.0f)
        {
            FFGBanditSpec Spec;
            Spec.Kind = EFGBanditKind::Boss;
            Spec.Mesh = TEXT("SK_Boss");
            Spec.Slot = FVector(PlayerForwardCm + 1000.0f, 0.0f, AFGTrain::RoofCm);
            Boss = SpawnBandit(Spec);
            ShowdownStep = 1;
        }
        break;
    case 1:     // waiting for OnBossLanded
        break;
    case 2:     // holster
        Prompt = TEXT("HOLSTER");
        SubPrompt = Player->Tracker->bTrackerLive ? TEXT("drop your gun hand to your hip") : TEXT("hold H");
        HolsteredFor = In.bHolstered ? HolsteredFor + DeltaTime : 0.0f;
        HolsterWait += DeltaTime;
        // A booth player who never finds the holster pose still gets their duel.
        if (HolsteredFor > 0.8f || HolsterWait > 9.0f)
        {
            Prompt = TEXT("WAIT FOR IT...");
            SubPrompt = TEXT("");
            ShowdownTimer = FMath::FRandRange(1.8f, 3.4f);
            ShowdownStep = 3;
            PlaySfx(TEXT("heartbeat"));
        }
        break;
    case 3:     // hands off until the whistle
        if (!In.bHolstered && ShowdownTimer > 0.15f && HolsterWait <= 9.0f)
        {
            Prompt = TEXT("TOO EARLY");
            HolsteredFor = 0.0f;
            ShowdownTimer = 1.2f;
            ShowdownStep = 5;
        }
        else if (ShowdownTimer <= 0.0f)
        {
            Prompt = TEXT("DRAW!");
            PlaySfx(TEXT("whistle"));
            DrawCalledAt = FPlatformTime::Seconds();
            if (Boss) { Boss->Draw(1.15f); }
            ShowdownStep = 4;
        }
        break;
    case 4:     // live. Resolved by OnBanditKilled, or by his bullet.
        if (PhaseTime > 0.0f && FPlatformTime::Seconds() - DrawCalledAt > 2.0) { Prompt = TEXT(""); }
        if (Boss && !Boss->IsDead() && FPlatformTime::Seconds() - DrawCalledAt > 3.0 && ShowdownTimer <= 0.0f)
        {
            ShowdownTimer = 2.2f;
            Boss->Draw(0.7f);           // he keeps shooting until someone drops
        }
        break;
    case 5:
        if (ShowdownTimer <= 0.0f) { ShowdownStep = 2; }
        break;
    case 6:     // aftermath
        if (ShowdownTimer <= 0.0f) { SetPhase(EFGPhase::Result); }
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

bool AFGIronHorseGameMode::PlayerMayFire() const
{
    return Phase != EFGPhase::Title && !(Phase == EFGPhase::Result && ResultTime < 1.5f);
}

bool AFGIronHorseGameMode::HandleUiShot(FVector2D Aim)
{
    if (Phase != EFGPhase::Result) { return false; }
    PlaySfx(TEXT("shot_player"));
    if (RideAgainButton().ExpandBy(0.03).IsInside(Aim))
    {
        UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
    }
    return true;
}

void AFGIronHorseGameMode::OnPlayerDryFire() {}
void AFGIronHorseGameMode::OnPlayerReloaded() {}

bool AFGIronHorseGameMode::ResolvePlayerShot(const FVector& Origin, const FVector& Dir, const FVector& Muzzle)
{
    PlaySfx(TEXT("shot_player"));
    UWorld* W = GetWorld();

    // Webcam aim is noisy, so be generous: anything within a few degrees of the ray counts. Nearest to the ray wins.
    const float Assist = Phase == EFGPhase::Bell ? 6.0f : AimAssistDegrees;
    AFGBandit* BestBandit = nullptr;
    AFGTarget* BestTarget = nullptr;
    float BestScore = 1.0f;
    FVector HitPoint = Origin + Dir * 6000.0f;
    bool bHead = false;

    const bool bShowdownLocked = Phase == EFGPhase::Showdown && ShowdownStep != 4;     // he can only be shot once DRAW is called
    for (AFGBandit* B : Bandits)
    {
        if (!B || B->IsDead() || (B == Boss && bShowdownLocked)) { continue; }
        FVector Chest, Head;
        B->AimPoints(Chest, Head);
        const float Dist = FVector::Dist(Origin, Chest);
        const float Allowed = FMath::Max(Assist, FMath::RadiansToDegrees(FMath::Atan(48.0f / Dist)));
        const float ChestScore = AngleBetween(Dir, Chest - Origin) / Allowed;
        const float HeadAngle = AngleBetween(Dir, Head - Origin);
        const bool bHeadHit = HeadAngle < FMath::RadiansToDegrees(FMath::Atan(17.0f / Dist)) + 0.35f;
        const float S = bHeadHit ? FMath::Min(ChestScore, 0.2f) : ChestScore;
        if (S < BestScore) { BestScore = S; BestBandit = B; BestTarget = nullptr; bHead = bHeadHit; HitPoint = bHeadHit ? Head : Chest; }
    }
    for (AFGTarget* T : Targets)
    {
        if (!T || !T->bActive) { continue; }
        const FVector C = T->Centre();
        const float Dist = FVector::Dist(Origin, C);
        const float Allowed = FMath::Max(Assist, FMath::RadiansToDegrees(FMath::Atan(T->Radius / Dist)));
        float S = AngleBetween(Dir, C - Origin) / Allowed;
        if (Phase == EFGPhase::Calibrate) { S = 0.0f; }     // any shot hits the bottle: that is what calibrates the tracker
        if (S < BestScore) { BestScore = S; BestTarget = T; BestBandit = nullptr; HitPoint = C; }
    }

    if (Phase == EFGPhase::Showdown && ShowdownStep == 3)
    {
        Prompt = TEXT("TOO EARLY");
        ShowdownTimer = 1.2f;
        ShowdownStep = 5;
    }

    const bool bHit = BestBandit || BestTarget;
    if (!bHit)
    {
        FHitResult Hit;
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(Player);
        Params.bTraceComplex = true;
        if (W->LineTraceSingleByChannel(Hit, Origin, Origin + Dir * 60000.0f, ECC_Visibility, Params))
        {
            HitPoint = Hit.ImpactPoint;
            AFGFx::Spawn(W, TEXT("fx"), TEXT("SM_Puff_Dust"), FTransform(FRotator::ZeroRotator, HitPoint, FVector(0.5f)), 0.7f, FVector(-TrainSpeed * 100.0f, 0, 120.0f), 3.0f);
        }
    }

    // Flash, tracer
    const FVector ToHit = (HitPoint - Muzzle).GetSafeNormal();
    const FQuat Along = ToHit.ToOrientationQuat() * FQuat(FRotator(0.0, -90.0, 0.0));     // fx meshes point along +Y
    if (AFGFx* Flash = AFGFx::Spawn(W, TEXT("fx"), TEXT("SM_MuzzleFlash_A"), FTransform(Along, Muzzle, FVector(0.6f)), 0.06f))
    {
        Flash->AddLight(FLinearColor(1.0f, 0.7f, 0.35f), 900.0f, 1500.0f);
    }
    const float TracerSpeed = 40000.0f;
    AFGFx::Spawn(W, TEXT("fx"), TEXT("SM_Tracer_Player"), FTransform(Along, Muzzle, FVector(1.0f, 2.0f, 1.0f)), FMath::Clamp(FVector::Dist(Muzzle, HitPoint) / TracerSpeed, 0.03f, 0.2f), ToHit * TracerSpeed);

    if (BestBandit)
    {
        BestBandit->bHeadshot = bHead;
        FHitResult Fake;
        Fake.ImpactPoint = Fake.Location = HitPoint;
        UGameplayStatics::ApplyPointDamage(BestBandit, bHead ? 100.0f : Player->ShotDamage, Dir, Fake, Player->GetController(), Player, nullptr);
        if (!BestBandit->IsDead()) { BestBandit->Play(TEXT("hit")); PlaySfx(TEXT("grunt")); }
        AFGFx::Spawn(W, TEXT("fx"), TEXT("SM_Puff_Dust"), FTransform(FRotator::ZeroRotator, HitPoint, FVector(0.25f)), 0.35f, FVector::ZeroVector, 4.0f);
        HitMarker = 1.0f;
    }
    else if (BestTarget)
    {
        BestTarget->Shot();
        HitMarker = 1.0f;
    }
    return bHit;
}

void AFGIronHorseGameMode::ShootablePoints(TArray<FVector>& Out) const
{
    const bool bBossLocked = Phase == EFGPhase::Showdown && ShowdownStep != 4;
    for (const AFGBandit* B : Bandits)
    {
        if (!B || B->IsDead() || (B == Boss && bBossLocked)) { continue; }
        FVector Chest, Head;
        B->AimPoints(Chest, Head);
        Out.Add(Chest);
    }
    for (const AFGTarget* T : Targets)
    {
        if (T && T->bActive) { Out.Add(T->Centre()); }
    }
}

void AFGIronHorseGameMode::TickMagnet(float DeltaTime)
{
    // Aim magnetism: near a target the crosshair leans onto it, harder the closer it gets. It hides the last of the
    // hand jitter exactly where it matters and makes a webcam feel like it is aiming for you, the way console shooters do.
    const FFGTrackerState& In = Player->Tracker->State;
    const FVector2D Raw(In.AimX, In.AimY);
    FVector2D Want = FVector2D::ZeroVector;
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    int32 W = 0, H = 0;
    if (PC) { PC->GetViewportSize(W, H); }
    if (PC && W > 0 && Player->Tracker->bTrackerLive && Phase != EFGPhase::Result && Phase != EFGPhase::Title)
    {
        constexpr float Reach = 0.13f;          // screen heights
        const float Aspect = float(W) / float(H);
        TArray<FVector> Points;
        ShootablePoints(Points);
        float BestD = Reach;
        for (const FVector& P : Points)
        {
            FVector2D Px;
            if (!PC->ProjectWorldLocationToScreen(P, Px)) { continue; }
            const FVector2D N(Px.X / W, Px.Y / H);
            const float D = FMath::Sqrt(FMath::Square((N.X - Raw.X) * Aspect) + FMath::Square(N.Y - Raw.Y));
            if (D < BestD)
            {
                BestD = D;
                Want = (N - Raw) * (0.75f * FMath::Pow(1.0f - D / Reach, 0.6f));
            }
        }
    }
    const float RealDelta = FApp::GetDeltaTime();
    MagnetOffset = FMath::Vector2DInterpTo(MagnetOffset, Want, RealDelta, 12.0f);
    AssistedAim = Raw + MagnetOffset;
}

bool AFGIronHorseGameMode::PlayerCanSee(const FVector& WorldPoint) const
{
    const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    int32 W = 0, H = 0;
    FVector2D Screen;
    if (!PC) { return false; }
    PC->GetViewportSize(W, H);
    if (W <= 0 || !PC->ProjectWorldLocationToScreen(WorldPoint, Screen)) { return false; }
    return Screen.X > W * 0.04f && Screen.X < W * 0.96f && Screen.Y > H * 0.06f && Screen.Y < H * 0.94f;
}

bool AFGIronHorseGameMode::RequestAttackToken()
{
    if (TokensOut >= MaxTokens || Phase == EFGPhase::Result || bPlayerDead) { return false; }
    ++TokensOut;
    return true;
}

void AFGIronHorseGameMode::ReleaseAttackToken()
{
    TokensOut = FMath::Max(0, TokensOut - 1);
}

void AFGIronHorseGameMode::OnTelegraph(AFGBandit* Bandit)
{
    // The bandit flashes red at the barrel for the 0.7 s (AFGBandit::Warning). Here: the rising tone.
    PlaySfx(TEXT("telegraph"), 0.8f);
}

void AFGIronHorseGameMode::OnBanditKilled(AFGBandit* Bandit)
{
    ++Kills;
    Score += 100;
    if (Bandit->bHeadshot) { ++Headshots; Score += 50; }
    PlaySfx(TEXT("grunt"));
    if (Bandit == Boss)
    {
        bBossBeaten = true;
        Score += 500;
        if (DrawTimeMs < 0.0f && ShowdownStep == 4)
        {
            DrawTimeMs = float((FPlatformTime::Seconds() - DrawCalledAt) * 1000.0);
            Score += FMath::Max(0, 1000 - int32(DrawTimeMs / 2.0f));
        }
        Prompt = TEXT("");
        ShowdownStep = 6;
        ShowdownTimer = 3.0f;
    }
}

void AFGIronHorseGameMode::SpawnEnemyShot(const FVector& From, bool bFast)
{
    // Aimed at where the head is now. It takes 0.7 s to arrive: move and it misses.
    FFGEnemyShot& Shot = Shots.AddDefaulted_GetRef();
    Shot.bAccurate = bFast || FMath::FRand() < 0.6f;
    Shot.Target = Player->HeadLocation();
    if (!Shot.bAccurate)
    {
        const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
        Shot.Target += FVector(0.0f, FMath::Cos(Angle), FMath::Sin(Angle) * 0.5f + 0.5f) * FMath::FRandRange(60.0f, 110.0f);
    }
    Shot.TimeLeft = bFast ? 0.3f : 0.7f;
    const FVector Dir = (Shot.Target - From).GetSafeNormal();
    const float Speed = FVector::Dist(From, Shot.Target) / Shot.TimeLeft;
    const FQuat Along = Dir.ToOrientationQuat() * FQuat(FRotator(0.0, -90.0, 0.0));
    AFGFx* Fx = AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_Tracer_Enemy"), FTransform(Along, From, FVector(1.4f)), Shot.TimeLeft + 0.35f, Dir * Speed);
    if (Fx) { Fx->AddLight(FLinearColor(1.0f, 0.35f, 0.15f), 400.0f, 900.0f); }
    Shot.Fx = Fx;
    if (AFGFx* Flash = AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_MuzzleFlash_Big"), FTransform(Along, From, FVector(0.8f)), 0.07f))
    {
        Flash->AddLight(FLinearColor(1.0f, 0.6f, 0.3f), 1200.0f, 2500.0f);
    }
    PlaySfx(TEXT("shot_enemy"), 0.9f);
}

void AFGIronHorseGameMode::SpawnDynamite(const FVector& From)
{
    // Lobbed at the player. Shoot it in the air, or lean out of the blast.
    const float Flight = 2.0f;
    const FVector Target = Player->HeadLocation() - FVector(0, 0, 60.0f);
    const float G = 980.0f;
    FVector V = (Target - From) / Flight;
    V.Z += 0.5f * G * Flight;
    AFGTarget* Stick = SpawnTarget(TEXT("props"), TEXT("SM_Dynamite"), FTransform(FRotator::ZeroRotator, From, FVector(2.6f)), 55.0f);
    Stick->Velocity = V;
    Stick->Gravity = G;
    Stick->Fuse = Flight;
    Stick->OnShot = [this](AFGTarget* T)
    {
        Score += 150;
        PlaySfx(TEXT("boom"));
        if (AFGFx* Boom = AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_MuzzleFlash_Big"), FTransform(FRotator::ZeroRotator, T->GetActorLocation(), FVector(3.0f)), 0.18f, FVector::ZeroVector, 6.0f))
        {
            Boom->AddLight(FLinearColor(1.0f, 0.6f, 0.25f), 6000.0f, 5000.0f);
        }
        AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_Puff_Smoke"), FTransform(FRotator::ZeroRotator, T->GetActorLocation(), FVector(1.0f)), 1.4f, FVector(-TrainSpeed * 100.0f, 0, 100.0f), 2.5f);
    };
    Stick->OnFuse = [this, Target](AFGTarget* T)
    {
        PlaySfx(TEXT("boom"));
        if (AFGFx* Boom = AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_MuzzleFlash_Big"), FTransform(FRotator::ZeroRotator, T->GetActorLocation(), FVector(4.0f)), 0.2f, FVector::ZeroVector, 6.0f))
        {
            Boom->AddLight(FLinearColor(1.0f, 0.6f, 0.25f), 8000.0f, 6000.0f);
        }
        const FVector Head = Player->HeadLocation();
        if (FMath::Abs(Head.Y - Target.Y) < 70.0f) { HurtPlayer(TEXT("hurt")); }
        else { ++Dodges; Score += 25; }
    };
}

void AFGIronHorseGameMode::TickShots(float DeltaTime)
{
    for (int32 i = Shots.Num() - 1; i >= 0; --i)
    {
        FFGEnemyShot& Shot = Shots[i];
        Shot.TimeLeft -= DeltaTime;
        if (Shot.TimeLeft > 0.0f) { continue; }
        const float Miss = FVector::Dist(Player->HeadLocation(), Shot.Target);
        if (Shot.bAccurate && Miss < Shot.Radius)
        {
            HurtPlayer(TEXT("hurt"));
        }
        else
        {
            if (Shot.bAccurate) { ++Dodges; Score += 25; }
            PlaySfx(TEXT("whiz"), 0.9f, FMath::FRandRange(0.9f, 1.15f));
        }
        Shots.RemoveAtSwap(i);
    }
}

void AFGIronHorseGameMode::HurtPlayer(const TCHAR* Sfx)
{
    if (bPlayerDead || bGod || InvulnerableFor > 0.0f || Phase == EFGPhase::Result) { return; }
    InvulnerableFor = 1.5f;
    PlaySfx(Sfx);
    const int32 Before = Player->Hats;
    const bool bDead = Player->TakeHit();
    if (Before == 3)
    {
        // First hit shoots your hat off. You see it go.
        AFGFx::Spawn(GetWorld(), TEXT("props"), TEXT("SM_Hat_Deputy"), FTransform(FRotator(0, 90, 0), Player->HeadLocation() + FVector(60, 0, 25)), 3.0f,
            FVector(900.0f, FMath::FRandRange(-200.f, 200.f), 350.0f), 0.0f, 600.0f, FRotator(300.0f, 200.0f, 0.0f));
    }
    if (bDead)
    {
        bPlayerDead = true;
        Player->bGunHidden = true;
        SetPhase(EFGPhase::Result);
    }
}

void AFGIronHorseGameMode::TickDuck()
{
    // Low things over the track: water tower spout, signal gantry, tunnel mouths.
    const float D = World->MetresTo(TEXT("duck"));
    bDuckWarning = D > 0.0f && D < FMath::Max(TrainSpeed, 8.0f) * 2.6f && TrainSpeed > 3.0f;
    // The event is measured at the player's car, the player stands a little ahead of its centre.
    const float Ahead = PlayerForwardCm / 100.0f;
    const bool bPassed = LastDuckDistance > Ahead && (D < 0.0f || D <= Ahead || D > LastDuckDistance + 5.0f);
    if (bPassed && TrainSpeed > 3.0f)
    {
        if (Player->Tracker->State.Duck < 0.45f) { HurtPlayer(TEXT("bonk")); }
        else { ++Dodges; Score += 50; PlaySfx(TEXT("whiz"), 1.0f, 0.6f); }
    }
    LastDuckDistance = D;
}

void AFGIronHorseGameMode::TickAtmosphere(float DeltaTime)
{
    const bool bDark = World->MetresTo(TEXT("dark")) == 0.0f;
    Darkness = FMath::FInterpTo(Darkness, bDark ? 1.0f : 0.0f, DeltaTime, 2.5f);
    if (Sun) { Sun->GetLightComponent()->SetIntensity(SunIntensity * (1.0f - 0.97f * Darkness)); }
    if (Sky) { Sky->GetLightComponent()->SetIntensity(1.3f * (1.0f - 0.9f * Darkness)); }
    if (Lantern) { Lantern->SetIntensity(Darkness * 320.0f); }
}

void AFGIronHorseGameMode::PlaySfx(const FString& Name, float Volume, float Pitch)
{
    if (USoundBase* Sound = FGAssets::Sound(Name))
    {
        UGameplayStatics::PlaySound2D(this, Sound, Volume, Pitch);
    }
}

FString AFGIronHorseGameMode::Rank() const
{
    if (Score >= 4200) { return TEXT("LEGEND"); }
    if (Score >= 3000) { return TEXT("GUNSLINGER"); }
    if (Score >= 1900) { return TEXT("DEPUTY"); }
    if (Score >= 900) { return TEXT("DRIFTER"); }
    return TEXT("GREENHORN");
}

float AFGIronHorseGameMode::Accuracy() const
{
    return Player && Player->ShotsFired > 0 ? float(Player->ShotsHit) / float(Player->ShotsFired) : 0.0f;
}
